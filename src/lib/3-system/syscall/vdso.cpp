#include "1-primitives/config/architecture.hpp"
#include "1-primitives/config/platform.hpp"
#include "1-primitives/integers/u64.hpp"
#include "3-system/elf/auxiliary_entry.hpp"
#include "3-system/elf/file_header.hpp"
#include "3-system/elf/hashed_name.hpp"
#include "3-system/elf/image.hpp"
#include "3-system/elf/symbol.hpp"
#include "3-system/syscall/auxiliary.hpp"
#include "3-system/syscall/vdso.hpp"


#if ca_platform(linux) && ca_architecture(x86_64)

[[nodiscard]] auto ca::syscall::vdso_symbol(const char* const name) noexcept -> void* {

	const auto base = ca::syscall::auxiliary(ca::elf::auxiliary_types::vdso);
	if (!base || *base == 0U) [[unlikely]]
		return nullptr;
	const auto image = ca::elf::image::at(*reinterpret_cast<const ca::elf::file_header*>(*base));
	if (!image) [[unlikely]]
		return nullptr;
	const auto symbol = image->find(ca::elf::hashed_name::of(ca::sv::str{name}));
	if (!symbol || symbol->type() != ca::elf::symbol_types::function)
		return nullptr;
	return reinterpret_cast<void*>(image->address(symbol->_value));
}

#else

[[nodiscard]] auto ca::syscall::vdso_symbol(const char* const name) noexcept -> void* {

	static_cast<void>(name);
	return nullptr;
}

#endif
