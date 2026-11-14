// expect: exit 0
// sanitize: undefined
// A UBSan check failing inside ca::with: the context fails with the check as its reason, at the
// line that failed, and the program goes on.
#include "1-primitives/integers/i32.hpp"
#include "1-primitives/limits.hpp"
#include "3-system/error/errc.hpp"
#include "3-system/trace/of.hpp"
#include "4-lifecycle/with.hpp"

static volatile ca::i32 largest = ca::limits<ca::i32>::max();
static volatile ca::i32 width = 40;
static volatile ca::i32 sink = 0;

auto main(void) -> int {

	auto overflowed = ca::with([](void) noexcept { sink = largest + 1; }, [](const ca::error*) noexcept {});
	if (overflowed || overflowed.error() != ca::errc::not_recoverable)
		return 1;
	const ca::trace::record first = ca::trace::of(overflowed.error());
	if (first.frames().size() != 1U || first.frames()[0].message() != ca::sv::str{"signed integer overflow"})
		return 2;
	if (first.frames()[0].where().line() != 17U)
		return 3;

	auto shifted = ca::with([](void) noexcept { sink = 1 << width; }, [](const ca::error*) noexcept {});
	if (shifted || ca::trace::of(shifted.error()).frames()[0].message() != ca::sv::str{"shift out of bounds"})
		return 4;

	auto fine = ca::with([](void) noexcept { sink = largest - 1; }, [](const ca::error*) noexcept {});
	if (!fine || sink != ca::limits<ca::i32>::max() - 1)
		return 5;
	return 0;
}
