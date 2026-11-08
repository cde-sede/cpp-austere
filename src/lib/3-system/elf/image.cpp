#include "1-primitives/integers/byte.hpp"
#include "2-vocabulary/span.hpp"
#include "3-system/elf/auxiliary_entry.hpp"
#include "3-system/elf/image.hpp"
#include "3-system/syscall/auxiliary.hpp"
#include "3-system/syscall/impl/read_own.hpp"


namespace {

	constexpr ca::u16 HIDDEN_VERSION = 0x8000U;

	/* Bias of a program without a program_headers segment: its ELF header sits right before
	   its program headers, in the segment mapping file offset 0. */
	[[nodiscard]] auto bias_from_header(const ca::borrowed_slice<const ca::elf::program_header> segments) noexcept -> ca::optional<ca::u64> {

		const ca::u64 table = reinterpret_cast<ca::u64>(segments.data());
		ca::elf::file_header header{};
		if (table < sizeof(header) || !ca::syscall::impl::read_own(table - sizeof(header), ca::span<ca::byte>{reinterpret_cast<ca::byte*>(&header), sizeof(header)}))
			return ca::optional<ca::u64>{};
		if (!header.is_elf64() || header._segments_offset != sizeof(header) || header._segment_count != segments.size())
			return ca::optional<ca::u64>{};
		for (const ca::elf::program_header& segment : segments) {
			if (segment._type == ca::elf::segment_types::load && segment._offset == 0U)
				return ca::optional<ca::u64>{table - sizeof(header) - segment._address};
		}
		return ca::optional<ca::u64>{};
	}

	/* Symbols in the table: nchain of DT_HASH, else one past the last chain of DT_GNU_HASH. */
	auto symbol_count(const ca::u32* const hash, const ca::u32* const gnu_hash) noexcept -> ca::usz {

		if (hash != nullptr)
			return hash[1];
		if (gnu_hash == nullptr)
			return 0U;
		const ca::u32 buckets = gnu_hash[0];
		const ca::u32 offset = gnu_hash[1];
		const ca::u32 words = gnu_hash[2];
		const ca::u32* const bucket = gnu_hash + 4U + 2U * words;
		const ca::u32* const chain = bucket + buckets;
		ca::u32 last = 0U;
		for (ca::u32 i = 0U; i < buckets; ++i) {
			if (bucket[i] > last)
				last = bucket[i];
		}
		if (last < offset)
			return offset;
		while ((chain[last - offset] & 1U) == 0U)
			++last;
		return last + 1U;
	}

} // namespace


[[nodiscard]] auto ca::elf::image::at(const ca::elf::file_header& header) noexcept -> ca::optional<ca::elf::image> {

	if (!header.is_elf64() || header._segment_count == 0U)
		return ca::optional<self>{};
	const ca::u64 base = reinterpret_cast<ca::u64>(&header);
	const ca::borrowed_slice<const ca::elf::program_header> segments{
		reinterpret_cast<const ca::elf::program_header*>(base + header._segments_offset), header._segment_count};
	for (const ca::elf::program_header& segment : segments) {
		if (segment._type == ca::elf::segment_types::load && segment._offset == 0U)
			return ca::optional<self>{self::of(segments, base - segment._address)};
	}
	return ca::optional<self>{};
}

[[nodiscard]] auto ca::elf::image::of(const ca::borrowed_slice<const ca::elf::program_header> segments, const ca::u64 bias) noexcept -> ca::elf::image {

	self out{};
	      out._bias = bias;
	  out._segments = segments;
	out._link_begin = ~ca::u64{0U};
	for (const ca::elf::program_header& segment : segments) {
		if (segment._type != ca::elf::segment_types::load)
			continue;
		if (segment._address < out._link_begin)
			out._link_begin = segment._address;
		if (segment._address + segment._memory_size > out._link_end)
			out._link_end = segment._address + segment._memory_size;
	}
	if (out._link_end == 0U)
		out._link_begin = 0U;
	const auto dynamic = out.segment(ca::elf::segment_types::dynamic);
	if (!dynamic)
		return out;
	const auto* const first = reinterpret_cast<const ca::elf::dynamic_entry*>(bias + dynamic->_address);
	ca::usz count = 0U;
	while (first[count]._tag != ca::elf::dynamic_tags::null)
		++count;
	out._dynamic = ca::borrowed_slice<const ca::elf::dynamic_entry>{first, count};
	for (const ca::elf::dynamic_entry& entry : out._dynamic) {
		const ca::u64 at = out._pointer(entry._value);
		if (entry._tag == ca::elf::dynamic_tags::strings_size)
			out._strings_size = entry._value;
		else if (at == 0U)
			continue;
		else if (entry._tag == ca::elf::dynamic_tags::strings)
			out._strings = reinterpret_cast<const char*>(at);
		else if (entry._tag == ca::elf::dynamic_tags::symbols)
			out._symbols = reinterpret_cast<const ca::elf::symbol*>(at);
		else if (entry._tag == ca::elf::dynamic_tags::hash)
			out._hash = reinterpret_cast<const ca::u32*>(at);
		else if (entry._tag == ca::elf::dynamic_tags::gnu_hash)
			out._gnu_hash = reinterpret_cast<const ca::u32*>(at);
		else if (entry._tag == ca::elf::dynamic_tags::version_symbols)
			out._versions = reinterpret_cast<const ca::u16*>(at);
	}
	if (out._symbols != nullptr)
		out._symbol_count = symbol_count(out._hash, out._gnu_hash);
	return out;
}

[[nodiscard]] auto ca::elf::image::bias_of(const ca::borrowed_slice<const ca::elf::program_header> segments) noexcept -> ca::optional<ca::u64> {

	for (const ca::elf::program_header& segment : segments) {
		if (segment._type == ca::elf::segment_types::program_headers)
			return ca::optional<ca::u64>{reinterpret_cast<ca::u64>(segments.data()) - segment._address};
	}
	return ca::optional<ca::u64>{};
}

[[nodiscard]] auto ca::elf::image::program(void) noexcept -> ca::optional<ca::elf::image> {

	const ca::u64 table = ca::syscall::auxiliary(ca::elf::auxiliary_types::program_headers).value_or(0U);
	const ca::u64 count = ca::syscall::auxiliary(ca::elf::auxiliary_types::program_header_count).value_or(0U);
	if (table == 0U || count == 0U)
		return ca::optional<self>{};
	const ca::borrowed_slice<const ca::elf::program_header> segments{reinterpret_cast<const ca::elf::program_header*>(table), static_cast<ca::usz>(count)};
	auto bias = self::bias_of(segments);
	if (!bias)
		bias = bias_from_header(segments);
	if (!bias)
		return ca::optional<self>{};
	return ca::optional<self>{self::of(segments, *bias)};
}

[[nodiscard]] auto ca::elf::image::segment(const ca::u32 type) const noexcept -> ca::optional<const ca::elf::program_header&> {

	for (const ca::elf::program_header& segment : _segments) {
		if (segment._type == type)
			return ca::optional<const ca::elf::program_header&>{segment};
	}
	return ca::optional<const ca::elf::program_header&>{};
}

[[nodiscard]] auto ca::elf::image::value(const ca::i64 tag) const noexcept -> ca::optional<ca::u64> {

	for (const ca::elf::dynamic_entry& entry : _dynamic) {
		if (entry._tag == tag)
			return ca::optional<ca::u64>{entry._value};
	}
	return ca::optional<ca::u64>{};
}

[[nodiscard]] auto ca::elf::image::address_of(const ca::i64 tag) const noexcept -> ca::optional<ca::u64> {

	const auto value = this->value(tag);
	const ca::u64 at = value ? this->_pointer(*value) : 0U;
	if (at == 0U)
		return ca::optional<ca::u64>{};
	return ca::optional<ca::u64>{at};
}

[[nodiscard]] auto ca::elf::image::words(const ca::i64 address_tag, const ca::i64 size_tag) const noexcept -> ca::borrowed_slice<const ca::u64> {

	const auto bytes = this->_table(address_tag, size_tag, sizeof(ca::u64));
	return ca::borrowed_slice<const ca::u64>{reinterpret_cast<const ca::u64*>(bytes.data()), bytes.size() / sizeof(ca::u64)};
}

[[nodiscard]] auto ca::elf::image::string(const ca::u64 offset) const noexcept -> ca::sv::str {

	if (_strings == nullptr || offset >= _strings_size)
		return ca::sv::str{};
	const ca::usz room = _strings_size - static_cast<ca::usz>(offset);
	for (ca::usz length = 0U; length < room; ++length) {
		if (_strings[offset + length] == '\0')
			return ca::sv::str{_strings + offset, length};
	}
	return ca::sv::str{};
}

[[nodiscard]] auto ca::elf::image::find(const ca::elf::hashed_name& name) const noexcept -> ca::optional<const ca::elf::symbol&> {

	const auto matches = [&](const ca::usz index) noexcept -> bool {
		const ca::elf::symbol& symbol = _symbols[index];
		return symbol.is_defined() && (_versions == nullptr || (_versions[index] & HIDDEN_VERSION) == 0U) && this->string(symbol._name) == name._text;
	};
	if (_symbols == nullptr)
		return ca::optional<const ca::elf::symbol&>{};
	if (_gnu_hash != nullptr) {
		const ca::u32 buckets = _gnu_hash[0];
		const ca::u32 offset = _gnu_hash[1];
		const ca::u32 words = _gnu_hash[2];
		const ca::u32 shift = _gnu_hash[3];
		if (buckets == 0U || words == 0U)
			return ca::optional<const ca::elf::symbol&>{};
		const auto* const bloom = reinterpret_cast<const ca::u64*>(_gnu_hash + 4U);
		const ca::u32* const bucket = _gnu_hash + 4U + 2U * words;
		const ca::u32* const chain = bucket + buckets;
		const ca::u64 word = bloom[(name._gnu / 64U) % words];
		const ca::u64 mask = (ca::u64{1U} << (name._gnu % 64U)) | (ca::u64{1U} << ((name._gnu >> shift) % 64U));
		if ((word & mask) != mask)
			return ca::optional<const ca::elf::symbol&>{};
		for (ca::u32 index = bucket[name._gnu % buckets]; index >= offset && index < _symbol_count; ++index) {
			const ca::u32 hash = chain[index - offset];
			if ((hash | 1U) == (name._gnu | 1U) && matches(index))
				return ca::optional<const ca::elf::symbol&>{_symbols[index]};
			if ((hash & 1U) != 0U)
				break;
		}
		return ca::optional<const ca::elf::symbol&>{};
	}
	if (_hash != nullptr) {
		const ca::u32 buckets = _hash[0];
		if (buckets == 0U)
			return ca::optional<const ca::elf::symbol&>{};
		const ca::u32* const chain = _hash + 2U + buckets;
		ca::usz hops = 0U;
		for (ca::u32 index = _hash[2U + name._sysv % buckets]; index != 0U && index < _symbol_count && hops < _symbol_count; index = chain[index], ++hops) {
			if (matches(index))
				return ca::optional<const ca::elf::symbol&>{_symbols[index]};
		}
	}
	return ca::optional<const ca::elf::symbol&>{};
}

[[nodiscard]] auto ca::elf::image::_table(const ca::i64 address_tag, const ca::i64 size_tag, const ca::usz entry_size) const noexcept -> ca::borrowed_slice<const ca::byte> {

	const auto address = this->value(address_tag);
	const auto size = this->value(size_tag);
	const ca::u64 at = address ? this->_pointer(*address) : 0U;
	if (at == 0U || !size)
		return ca::borrowed_slice<const ca::byte>{};
	return ca::borrowed_slice<const ca::byte>{reinterpret_cast<const ca::byte*>(at), static_cast<ca::usz>(*size / entry_size * entry_size)};
}

[[nodiscard]] auto ca::elf::image::_pointer(const ca::u64 value) const noexcept -> ca::u64 {

	if (value >= _link_begin && value < _link_end)
		return _bias + value;
	if (value >= _bias + _link_begin && value < _bias + _link_end)
		return value;
	return 0U;
}

[[nodiscard]] auto ca::elf::image::relocations(void) const noexcept -> ca::borrowed_slice<const ca::elf::relocation> {

	const auto bytes = this->_table(ca::elf::dynamic_tags::rela, ca::elf::dynamic_tags::rela_size, sizeof(ca::elf::relocation));
	return ca::borrowed_slice<const ca::elf::relocation>{reinterpret_cast<const ca::elf::relocation*>(bytes.data()), bytes.size() / sizeof(ca::elf::relocation)};
}

[[nodiscard]] auto ca::elf::image::plt_relocations(void) const noexcept -> ca::borrowed_slice<const ca::elf::relocation> {

	const auto kind = this->value(ca::elf::dynamic_tags::plt_relocation_kind);
	if (!kind || *kind != static_cast<ca::u64>(ca::elf::dynamic_tags::rela))
		return ca::borrowed_slice<const ca::elf::relocation>{};
	const auto bytes = this->_table(ca::elf::dynamic_tags::plt_relocations, ca::elf::dynamic_tags::plt_relocations_size, sizeof(ca::elf::relocation));
	return ca::borrowed_slice<const ca::elf::relocation>{reinterpret_cast<const ca::elf::relocation*>(bytes.data()), bytes.size() / sizeof(ca::elf::relocation)};
}

[[nodiscard]] auto ca::elf::image::relative_relocations(void) const noexcept -> ca::borrowed_slice<const ca::u64> {

	const auto bytes = this->_table(ca::elf::dynamic_tags::relr, ca::elf::dynamic_tags::relr_size, sizeof(ca::u64));
	return ca::borrowed_slice<const ca::u64>{reinterpret_cast<const ca::u64*>(bytes.data()), bytes.size() / sizeof(ca::u64)};
}
