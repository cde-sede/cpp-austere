#!/bin/sh
# Source coverage of the library by the tests; called by `make coverage` after the tests ran in
# a clang COVERAGE=1 build, BUILD set to its directory. Each test binary wrote
# BUILD/tests/NAME.profraw (tooling/test.sh); they are merged and reported over src/lib.
#
#   tooling/coverage.sh OUT
#
# OUT/index.html: per file, then per line (llvm-cov show); OUT/summary.txt: lines, functions,
# regions and branches per file (llvm-cov report). Uses the llvm tools of clang's version.

out=$1
major=$(clang++ --version | sed -n 's/.*clang version \([0-9]*\).*/\1/p' | head -1)
profdata=llvm-profdata-$major
cov=llvm-cov-$major
command -v "$profdata" >/dev/null 2>&1 || profdata=llvm-profdata
command -v "$cov" >/dev/null 2>&1 || cov=llvm-cov

set -- "$BUILD"/tests/*.profraw
if [ ! -e "$1" ]; then
	echo "coverage: no profile in $BUILD/tests (run make coverage)"
	exit 1
fi
merged=$BUILD/coverage.profdata
"$profdata" merge -sparse "$@" -o "$merged" || exit 1

objects=
first=
for profile in "$@"; do
	binary=${profile%.profraw}
	[ -x "$binary" ] || continue
	if [ -z "$first" ]; then
		first=$binary
	else
		objects="$objects -object $binary"
	fi
done

rm -rf "$out"
mkdir -p "$out"
# shellcheck disable=SC2086
"$cov" show "$first" $objects -instr-profile="$merged" -format=html -output-dir="$out" \
	-show-branches=count -ignore-filename-regex='(tests|fuzz)/' src/lib || exit 1
# shellcheck disable=SC2086
"$cov" report "$first" $objects -instr-profile="$merged" -show-branch-summary \
	-ignore-filename-regex='(tests|fuzz)/' src/lib > "$out/summary.txt" || exit 1
total=$(grep '^TOTAL' "$out/summary.txt")
echo "coverage: $out/index.html, $out/summary.txt"
printf '%s\n' "$total" | awk '{ printf "  regions %s  functions %s  lines %s  branches %s\n", $4, $7, $10, $13 }'
