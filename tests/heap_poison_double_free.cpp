// expect: trap
// sanitize: address
// ca::heap freeing a block twice traps under ca_memory_poisoning, whatever its geometry.
#include "4-lifecycle/memory/heap.hpp"

auto main(void) -> int {

	const ca::heap heap{};
	auto kept = heap.allocate(64U, 8U);
	auto block = heap.allocate(64U, 8U);
	if (!kept || !block)
		return 1;
	heap.deallocate(*block, 8U);
	heap.deallocate(*block, 8U);
	return 0;
}
