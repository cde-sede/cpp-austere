// Readers of untrusted ELF bytes: elf::file (headers, sections, segments, names, string and
// symbol tables) and the DWARF reader of the symbolizer (trace::impl::read_image, locate at
// addresses taken from the input and from the symbols). Nothing may read outside the bytes:
// every view they return is checked to lie inside. Seeded with real objects (make fuzz builds
// build/fuzz/seeds/elf from tests/dl/base.cpp).
#include "1-primitives/integers/u64.hpp"
#include "3-system/elf/file.hpp"
#include "3-system/trace/impl/debug_info.hpp"
#include "3-system/trace/location.hpp"
#include "fuzz/fuzz.hpp"

namespace {

	auto inside(const ca::span<const ca::byte> all, const ca::span<const ca::byte> part) noexcept -> bool {
		if (part.size() == 0U)
			return true;
		return part.data() >= all.data() && part.data() + part.size() <= all.data() + all.size();
	}

	auto inside(const ca::span<const ca::byte> all, const ca::sv::str text) noexcept -> bool {
		return inside(all, ca::span<const ca::byte>{reinterpret_cast<const ca::byte*>(text.data()), text.size()});
	}

	auto walk(const ca::elf::file& file, const ca::span<const ca::byte> all) noexcept -> ca::u64 {
		ca::u64 sum = 0U;
		for (const ca::elf::program_header& segment : file.segments())
			fuzz::require(inside(all, file.contents(segment)), "segment contents outside the file");
		for (const ca::elf::section_header& section : file.sections()) {
			fuzz::require(inside(all, file.contents(section)), "section contents outside the file");
			fuzz::require(inside(all, file.name(section)), "section name outside the file");
			const auto strings = file.linked(section);
			for (const ca::elf::symbol& symbol : file.symbols(section)) {
				sum += symbol._value;
				if (strings) {
					const ca::sv::str name = file.string(*strings, symbol._name);
					fuzz::require(inside(all, name), "symbol name outside the file");
				}
			}
		}
		static_cast<void>(file.find(".symtab"));
		static_cast<void>(file.find(".debug_info"));
		return sum;
	}

} // namespace

extern "C" auto LLVMFuzzerTestOneInput(const ca::u8* const data, const ca::usz size) -> int {

	const ca::span<const ca::byte> all{reinterpret_cast<const ca::byte*>(data), size};
	const auto file = ca::elf::file::from(all);
	ca::u64 symbol_address = 0U;
	if (file)
		symbol_address = walk(*file, all);
	const auto image = ca::trace::impl::read_image(all);
	if (!image)
		return 0;
	ca::trace::location out[8];
	const ca::u64 addresses[] = {0U, symbol_address, size > 16U ? static_cast<ca::u64>(data[size - 1U]) << 4U : 0x1000U, 0x401000U};
	for (const ca::u64 address : addresses) {
		const ca::usz found = ca::trace::impl::locate(*image, address, ca::span<ca::trace::location>{out, 8U});
		fuzz::require(found <= 8U, "locate wrote past its output");
		for (ca::usz i = 0U; i < found; ++i)
			fuzz::require(inside(all, out[i].file()) || out[i].file().empty() || out[i].file().data() == nullptr, "location file outside the file");
	}
	return 0;
}
