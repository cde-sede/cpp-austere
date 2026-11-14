// expect: trap
#include "4-lifecycle/memory/arena.hpp"

auto main(void) -> int {

	auto made = ca::arena::make();
	if (!made)
		return 1;
	const ca::arena_scope outer = made->push();
	if (!outer.allocate(16U, 8U))
		return 2;
	const ca::arena_scope inner = outer.push().push();
	outer.pop();
	inner.pop();
	return 0;
}
