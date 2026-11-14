// expect: report use-after-poison
// sanitize: address
// The room of an arena past its newest block is poisoned: a write one past the block is seen.
#include "4-lifecycle/memory/arena.hpp"

auto main(void) -> int {

	auto made = ca::arena::make();
	if (!made)
		return 0;
	auto block = made->allocator().allocate(24U, 8U);
	if (!block)
		return 0;
	volatile char* const data = static_cast<char*>(block->data());
	data[23] = 1;
	data[24] = 1;
	return 0;
}
