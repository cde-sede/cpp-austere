// expect: error value accessed without a check
// mode: debug
// sanitize: !undefined !coverage
#include "2-vocabulary/fail.hpp"
#include "2-vocabulary/result.hpp"

constexpr auto f(void) noexcept -> ca::result<int> {
	return ca::fail(5);
}

auto main(void) -> int {
	auto v = f();
	return v.error().code();
}
