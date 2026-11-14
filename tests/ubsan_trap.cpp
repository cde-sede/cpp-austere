// expect: report ca: undefined behavior: signed integer overflow at tests/ubsan_trap\.cpp:13:
// sanitize: undefined
// A UBSan check failing outside every context: one line on stderr, then a trap; linked against
// libc and without it, with ca's handlers in place of UBSan's runtime.
#include "1-primitives/integers/i32.hpp"
#include "1-primitives/limits.hpp"

static volatile ca::i32 largest = ca::limits<ca::i32>::max();

auto main(void) -> int {

	const ca::i32 value = largest;
	return value + 1 == 0;
}
