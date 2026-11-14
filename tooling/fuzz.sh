#!/bin/sh
# Runs one libFuzzer target for a while; called by `make fuzz` and `make fuzz-smoke`.
#
#   tooling/fuzz.sh BIN SECONDS JOBS
#
# BIN is build/<config>/fuzz/NAME. Its corpus grows in build/fuzz/corpus/NAME, seeded from
# fuzz/seeds/NAME and build/fuzz/seeds/NAME (what the Makefile builds: ELF objects) when they
# exist; inputs that break it land in build/fuzz/crashes/NAME/
# (rerun one: BIN build/fuzz/crashes/NAME/crash-...). Prints one line; fails when the run found
# something.

bin=$1
seconds=$2
jobs=$3
name=$(basename "$bin")
corpus=build/fuzz/corpus/$name
crashes=build/fuzz/crashes/$name
log=build/fuzz/logs/$name.log
mkdir -p "$corpus" "$crashes" "$(dirname "$log")"
seeds=
[ -d "fuzz/seeds/$name" ] && seeds=fuzz/seeds/$name
[ -d "build/fuzz/seeds/$name" ] && seeds="$seeds build/fuzz/seeds/$name"

export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1:allocator_may_return_null=1}"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-print_stacktrace=1}"

set -- -max_total_time="$seconds" -max_len=4096 -rss_limit_mb=4096 -timeout=20 \
	-artifact_prefix="$crashes/" -print_final_stats=1
[ "$jobs" -gt 1 ] && set -- "$@" -jobs="$jobs" -workers="$jobs"

# shellcheck disable=SC2086
"$bin" "$@" "$corpus" $seeds > "$log" 2>&1
status=$?
runs=$(sed -n 's/^stat::number_of_executed_units: *//p' "$log" | tail -1)
size=$(find "$corpus" -type f | wc -l)
if [ $status -eq 0 ]; then
	printf '  ok    %-16s %s runs, corpus %s\n' "$name" "${runs:-?}" "$size"
else
	printf '  FAIL  %-16s status %s, see %s\n' "$name" "$status" "$log"
	grep -m3 -E 'ERROR|runtime error|fuzz: broken|undefined behavior|Test unit written' "$log" | sed 's/^/          /'
fi
exit $status
