// expect: report use-after-poison
// sanitize: address
// The rule of arena_scope broken: a block allocated through an enclosing scope while a nested
// one is open on the same lane is released by the nested pop; its next use is seen.
#include "4-lifecycle/memory/arena.hpp"

auto main(void) -> int {

	auto made = ca::arena::make();
	if (!made)
		return 0;
	const ca::arena_scope outer = made->push();
	const ca::arena_scope nested = outer.push().push();
	if (!nested.allocate(64U, 8U))
		return 0;
	auto misplaced = outer.allocate(32U, 8U);
	if (!misplaced)
		return 0;
	nested.pop();
	volatile char* const data = static_cast<char*>(misplaced->data());
	data[0] = 1;
	outer.pop();
	return 0;
}
