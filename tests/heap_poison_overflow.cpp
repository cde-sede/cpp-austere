// expect: report use-after-poison
// sanitize: address
// The byte past a block of ca::heap, in a free neighbour of its class, written.
#include "4-lifecycle/memory/heap.hpp"

auto main(void) -> int {

	const ca::heap heap{};
	auto block = heap.allocate(32U, 8U);
	if (!block || block->size() != 32U)
		return 0;
	volatile char* const data = static_cast<char*>(block->data());
	data[32] = 1;
	heap.deallocate(*block, 8U);
	return 0;
}
