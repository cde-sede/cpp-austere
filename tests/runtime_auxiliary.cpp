// expect: exit 0
// link: freestanding
// _start keeps the auxiliary vector it reads, so syscall::auxiliary needs no /proc.
#include "3-system/elf/auxiliary_entry.hpp"
#include "3-system/elf/image.hpp"
#include "3-system/syscall/auxiliary.hpp"

auto main(int, char**, char** envp) -> int {
	const ca::elf::auxiliary_entry* const kept = ca::syscall::impl::auxiliary_vector;
	if (kept == nullptr)
		return 1;
	char** end = envp;
	while (*end != nullptr)
		++end;
	if (reinterpret_cast<const void*>(end + 1) != kept)
		return 2;
	if (ca::syscall::auxiliary(ca::elf::auxiliary_types::page_size).value_or(0U) != 4096U)
		return 3;
	if (ca::syscall::auxiliary(ca::elf::auxiliary_types::vdso).value_or(0U) == 0U)
		return 4;
	if (ca::syscall::auxiliary(ca::elf::auxiliary_types::null))
		return 5;
	if (ca::syscall::auxiliary(~ca::u64{0U}))
		return 6;
	if (ca::syscall::auxiliary(ca::elf::auxiliary_types::secure).value_or(1U) != 0U)
		return 8;
	if (!ca::elf::image::program())
		return 7;
	return 0;
}
