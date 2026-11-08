#include "1-primitives/config/architecture.hpp"
#include "1-primitives/config/platform.hpp"
#include "3-system/elf/auxiliary_entry.hpp"
#include "3-system/syscall/auxiliary.hpp"
#include "3-system/syscall/restarting.hpp"
#include "3-system/syscall/syscalls.hpp"

#include <fcntl.h>


constinit const ca::elf::auxiliary_entry* ca::syscall::impl::auxiliary_vector = nullptr;

[[nodiscard]] auto ca::syscall::auxiliary(const ca::u64 type) noexcept -> ca::optional<ca::u64> {

#if ca_platform(linux) && ca_architecture(x86_64)
	if (const ca::elf::auxiliary_entry* entry = ca::syscall::impl::auxiliary_vector) {
		for (; entry->_type != ca::elf::auxiliary_types::null; ++entry) {
			if (entry->_type == type)
				return ca::optional<ca::u64>{entry->_value};
		}
		return ca::optional<ca::u64>{};
	}
	const auto fd = ca::syscall::restarting([] {
		return ca::syscall::openat(AT_FDCWD, "/proc/self/auxv", O_RDONLY | O_CLOEXEC, 0);
	});
	if (!fd) [[unlikely]]
		return ca::optional<ca::u64>{};
	ca::elf::auxiliary_entry entries[64] = {};
	ca::usz filled = 0U;
	for (;;) {
		const auto done = ca::syscall::restarting([&] {
			return ca::syscall::read(*fd, reinterpret_cast<char*>(entries) + filled, sizeof(entries) - filled);
		});
		if (!done || *done == 0)
			break;
		filled += static_cast<ca::usz>(*done);
	}
	static_cast<void>(ca::syscall::close(*fd));
	for (ca::usz i = 0U; i < filled / sizeof(ca::elf::auxiliary_entry); ++i) {
		if (entries[i]._type == ca::elf::auxiliary_types::null)
			break;
		if (entries[i]._type == type)
			return ca::optional<ca::u64>{entries[i]._value};
	}
	return ca::optional<ca::u64>{};
#else
	static_cast<void>(type);
	return ca::optional<ca::u64>{};
#endif
}
