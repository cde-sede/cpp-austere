// expect: exit 0
#include "2-vocabulary/borrowed_slice.hpp"
#include "2-vocabulary/sv/str.hpp"
#include "3-system/elf/auxiliary_entry.hpp"
#include "3-system/elf/file_header.hpp"
#include "3-system/elf/hashed_name.hpp"
#include "3-system/elf/image.hpp"
#include "3-system/elf/program_header.hpp"
#include "3-system/syscall/auxiliary.hpp"

static_assert(ca::elf::hashed_name::of(ca::sv::str{""})._gnu == 5381U);
static_assert(ca::elf::hashed_name::of(ca::sv::str{"printf"})._gnu == 0x156B2BB8U);
static_assert(ca::elf::hashed_name::of(ca::sv::str{"printf"})._sysv == 0x077905A6U);

constexpr ca::elf::symbol weak_hidden_function{0U, 0x22U, ca::elf::symbol_visibilities::hidden, 1U, 0U, 0U};
static_assert(weak_hidden_function.type() == ca::elf::symbol_types::function);
static_assert(weak_hidden_function.binding() == ca::elf::symbol_bindings::weak);
static_assert(weak_hidden_function.visibility() == ca::elf::symbol_visibilities::hidden);
static_assert(weak_hidden_function.is_defined());

constexpr ca::elf::relocation global_data{0U, (ca::u64{7U} << 32U) | ca::elf::x86_64_relocations::global_data, 0};
static_assert(global_data.symbol() == 7U && global_data.type() == ca::elf::x86_64_relocations::global_data);

static auto vdso(void) noexcept -> int {
	const ca::u64 base = ca::syscall::auxiliary(ca::elf::auxiliary_types::vdso).value_or(0U);
	if (base == 0U)
		return 0;
	const auto image = ca::elf::image::at(*reinterpret_cast<const ca::elf::file_header*>(base));
	if (!image || image->dynamic().empty() || image->symbols().empty())
		return 1;
	const auto clock = image->find(ca::elf::hashed_name::of(ca::sv::str{"__vdso_clock_gettime"}));
	if (!clock || clock->type() != ca::elf::symbol_types::function || image->string(clock->_name) != ca::sv::str{"__vdso_clock_gettime"})
		return 2;
	const ca::u64 address = image->address(clock->_value);
	bool inside = false;
	for (const ca::elf::program_header& segment : image->segments()) {
		if (segment._type == ca::elf::segment_types::load && (segment._flags & ca::elf::segment_flags::execute) != 0U)
			inside = inside || (address >= image->address(segment._address) && address < image->address(segment._address + segment._memory_size));
	}
	if (!inside)
		return 3;
	if (image->find(ca::elf::hashed_name::of(ca::sv::str{"__vdso_no_such_function"})))
		return 4;
	for (const ca::elf::symbol& symbol : image->symbols()) {
		if (symbol.is_defined() && !image->string(symbol._name).empty()) {
			const auto again = image->find(ca::elf::hashed_name::of(image->string(symbol._name)));
			if (!again || &*again != &symbol)
				return 5;
		}
	}
	if (!image->value(ca::elf::dynamic_tags::strings) || image->value(ca::elf::dynamic_tags::null))
		return 6;
	return 0;
}

static auto image_has_phdr(const ca::borrowed_slice<const ca::elf::program_header> segments) noexcept -> bool {
	for (const ca::elf::program_header& segment : segments) {
		if (segment._type == ca::elf::segment_types::program_headers)
			return true;
	}
	return false;
}

static auto program(void) noexcept -> int {
	const ca::u64 table = ca::syscall::auxiliary(ca::elf::auxiliary_types::program_headers).value_or(0U);
	const ca::u64 count = ca::syscall::auxiliary(ca::elf::auxiliary_types::program_header_count).value_or(0U);
	if (table == 0U || count == 0U)
		return 10;
	const ca::borrowed_slice<const ca::elf::program_header> segments{reinterpret_cast<const ca::elf::program_header*>(table), static_cast<ca::usz>(count)};
	const auto found = ca::elf::image::program();
	if (!found || found->segments().data() != segments.data() || found->segments().size() != segments.size())
		return 16;
	const auto bias = ca::elf::image::bias_of(segments);
	if (bias.has_value() != image_has_phdr(segments) || (bias && *bias != found->bias()))
		return 17;
	const ca::elf::image& image = *found;
	if (!image.segment(ca::elf::segment_types::load) || image.segment(ca::elf::segment_types::null + 0x1234U))
		return 11;
	const auto* const header = reinterpret_cast<const ca::elf::file_header*>(segments.data()) - 1;
	const auto load = image.segment(ca::elf::segment_types::load);
	if (load && load->_offset == 0U && !bias && image.address(load->_address) != reinterpret_cast<ca::u64>(header))
		return 18;
	if (image.segment(ca::elf::segment_types::dynamic).has_value() == image.dynamic().empty())
		return 19;
	const ca::u64 here = reinterpret_cast<ca::u64>(&program);
	bool inside = false;
	for (const ca::elf::program_header& segment : segments) {
		if (segment._type == ca::elf::segment_types::load)
			inside = inside || (here >= image.address(segment._address) && here < image.address(segment._address + segment._memory_size));
	}
	if (!inside)
		return 12;
	for (const ca::elf::symbol& symbol : image.symbols()) {
		if (symbol._name != 0U && image.string(symbol._name).empty())
			return 13;
	}
	ca::usz relocations = 0U;
	for (const ca::elf::relocation& relocation : image.relocations()) {
		if (!image.segment(ca::elf::segment_types::load) || relocation._offset == 0U)
			return 14;
		++relocations;
	}
	if (!image.dynamic().empty() && image.value(ca::elf::dynamic_tags::rela) && relocations == 0U)
		return 15;
	return 0;
}

auto main(void) -> int {
	const int first = vdso();
	return first != 0 ? first : program();
}
