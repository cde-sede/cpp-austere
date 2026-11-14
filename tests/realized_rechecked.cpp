// expect: error value accessed without a check
// mode: debug
// sanitize: !undefined !coverage !address
#include "2-vocabulary/result.hpp"

constexpr auto f(void) noexcept -> ca::result<int> {
	return 21;
}

auto main(void) -> int {
	auto v = f();
	if (!v)
		return 1;
	v = 3;
	return *v;
}
