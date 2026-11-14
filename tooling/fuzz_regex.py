#!/usr/bin/env python3
"""Differential fuzzing of ca::regex against CPython's re: random patterns, flags, operations
and texts, as tooling/gen_regex_test.py makes them, but from a new seed each run and as many as
asked. Every case's expectation is what the running Python returns; the cases are compiled in
batches into a program over the library of the current build (tests/regex_python.cpp's runner)
and run. Called by `make fuzz-regex`, which sets CXX, CXXFLAGS, LDFLAGS, LIB and BUILD; a
sanitized build (SAN=address,undefined) watches the engine at the same time.

    tooling/fuzz_regex.py [--cases N] [--seed S] [--batch B]

Each divergence is printed (pattern, text, ca's answer, Python's); the batch program stays in
BUILD/fuzz/regex/, and --seed S replays a run exactly. Exits 1 when ca and Python disagreed.
"""

import argparse
import os
import random
import subprocess
import sys
import time

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_regex_test as gen  # noqa: E402


def cases(rng, count):
	out = []
	while len(out) < count:
		text = ''.join(rng.choice(gen.ALPHABET) for _ in range(rng.randint(0, 16)))
		case = (rng.choice(gen.OPS + ['search2', 'match2', 'split1', 'sub1']), rng.choice(gen.FLAGS), gen.pattern(rng), text, rng.choice(gen.REPLS))
		if text == '' and '\\B' in case[2]:
			continue
		out.append(case)
	return out


def program(batch):
	rows = []
	for op, flags, source, text, replacement in batch:
		want = gen.expected(op, flags, source, text, replacement)
		pos = gen.offset(text, min(2, len(text))) if op.endswith('2') else 0
		rows.append(f'\t\t{{{gen.literal(op)}, {gen.literal(flags)}, {gen.view(source)}, {gen.view(text)}, {gen.view(replacement)}, {gen.view(want)}, {pos}U}},')
	return gen.TEMPLATE.replace('@ROWS@', '\n'.join(rows))


def run(source, directory, index, env):
	cpp = os.path.join(directory, f'batch{index}.cpp')
	binary = os.path.join(directory, f'batch{index}')
	with open(cpp, 'w') as f:
		f.write(source)
	flags = env['CXXFLAGS'].split()
	compiled = subprocess.run([env['CXX'], *flags, '-c', cpp, '-o', binary + '.o'], capture_output=True, text=True)
	if compiled.returncode != 0:
		sys.exit(f'fuzz_regex: {cpp} does not build:\n{compiled.stderr[:2000]}')
	linked = subprocess.run([env['CXX'], binary + '.o', '-o', binary, env['LIB'], *env['LDFLAGS'].split()], capture_output=True, text=True)
	if linked.returncode != 0:
		sys.exit(f'fuzz_regex: {binary} does not link:\n{linked.stderr[:2000]}')
	ran = subprocess.run([binary], capture_output=True, text=True, errors='replace')
	return ran.returncode, ran.stdout + ran.stderr, cpp


def main():
	ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
	ap.add_argument('--cases', type=int, default=20000)
	ap.add_argument('--seed', type=int, default=None)
	ap.add_argument('--batch', type=int, default=2500)
	opts = ap.parse_args()
	env = {k: os.environ.get(k, '') for k in ('CXX', 'CXXFLAGS', 'LDFLAGS', 'LIB', 'BUILD')}
	if not env['CXX'] or not env['LIB']:
		sys.exit('fuzz_regex: run through make fuzz-regex (CXX, CXXFLAGS, LDFLAGS, LIB, BUILD)')
	seed = opts.seed if opts.seed is not None else int(time.time() * 1000) % 2**31
	directory = os.path.join(env['BUILD'] or 'build', 'fuzz', 'regex')
	os.makedirs(directory, exist_ok=True)
	all_cases = cases(random.Random(seed), opts.cases)
	failed = 0
	for index in range(0, len(all_cases), opts.batch):
		status, output, cpp = run(program(all_cases[index:index + opts.batch]), directory, index // opts.batch, env)
		if status != 0:
			failed += 1
			print(f'  FAIL  batch {index // opts.batch} ({cpp}), status {status}:')
			for line in output.splitlines()[:20]:
				print('        ' + line)
	verdict = 'ok   ' if failed == 0 else 'FAIL '
	print(f'  {verdict} fuzz-regex: {len(all_cases)} cases against Python {sys.version.split()[0]}, seed {seed}'
	      + ('' if failed == 0 else f', {failed} batches diverged (replay: make fuzz-regex SEED={seed} N={opts.cases})'))
	return 1 if failed else 0


if __name__ == '__main__':
	sys.exit(main())
