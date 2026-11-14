// expect: error value accessed without a check
// mode: debug
// sanitize: !undefined !coverage
#include "2-vocabulary/optional.hpp"

static auto twice(const ca::realized<int>& value) noexcept -> int {
	return *value * 2;
}

auto main(void) -> int {
	const ca::optional<int> o = 21;
	return twice(o);
}
