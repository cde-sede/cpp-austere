// expect: report use-after-poison
// sanitize: address
// A block of ca::heap read after its free: ASan sees it through the heap's poisoning.
#include "4-lifecycle/memory/heap.hpp"

auto main(void) -> int {

	const ca::heap heap{};
	auto block = heap.allocate(48U, 8U);
	if (!block)
		return 0;
	volatile char* const data = static_cast<char*>(block->data());
	data[20] = 1;
	heap.deallocate(*block, 8U);
	return data[20];
}
