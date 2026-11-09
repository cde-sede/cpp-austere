// expect: exit 0
// A segment written through one attachment is read through another; removed, it stays usable
// until detached.
#include "1-primitives/integers/byte.hpp"
#include "2-vocabulary/span.hpp"
#include "3-system/memory/shared_segment.hpp"

auto main(void) -> int {
	auto id = ca::sys::shm_create(1U << 16U);
	if (!id)
		return 1;
	auto first = ca::sys::shm_attach(*id, 1U << 16U);
	auto second = ca::sys::shm_attach(*id, 1U << 16U);
	if (!first || !second || first->data() == second->data() || first->size() != (1U << 16U))
		return 2;
	if (!ca::sys::shm_remove(*id))
		return 3;
	(*first)[0] = ca::byte{0x5A};
	(*first)[65535] = ca::byte{0xA5};
	if ((*second)[0] != ca::byte{0x5A} || (*second)[65535] != ca::byte{0xA5})
		return 4;
	if (!ca::sys::shm_detach(*first) || !ca::sys::shm_detach(*second))
		return 5;
	auto gone = ca::sys::shm_attach(*id, 1U << 16U);
	return gone ? 6 : 0;
}
