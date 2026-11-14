// expect: error value accessed without a check
// mode: debug
// sanitize: !undefined !coverage !address
#include "5-collections/array/dynamic_array.hpp"

auto main(void) -> int {
	ca::dynamic_array<int> a;
	if (!a.append(1))
		return 1;
	auto g = a.get(0);
	return *g;
}
