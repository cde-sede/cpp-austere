// ca's byte primitives (memcpy, memmove, memset, memcmp, memchr, strlen and their unit-wide
// forms, units compared as unsigned) against byte-at-a-time loops, on buffers of the exact size from ASan's malloc, at
// every offset and length: an overread past the end, or a wrong byte, stops the run.
#include "1-primitives/integers/uint_of.hpp"
#include "1-primitives/memory/compare_bytes.hpp"
#include "1-primitives/memory/compare_units.hpp"
#include "1-primitives/memory/copy_bytes.hpp"
#include "1-primitives/memory/equal_units.hpp"
#include "1-primitives/memory/fill_bytes.hpp"
#include "1-primitives/memory/find_byte.hpp"
#include "1-primitives/memory/find_unit.hpp"
#include "1-primitives/memory/move_bytes.hpp"
#include "1-primitives/memory/string_length.hpp"
#include "fuzz/fuzz.hpp"

extern "C" auto malloc(ca::usz size) noexcept -> void*;
extern "C" auto free(void* block) noexcept -> void;

namespace {

	constexpr ca::usz LARGEST = 1U << 14U;

	auto sign(const int value) noexcept -> int {
		return value < 0 ? -1 : value > 0 ? 1 : 0;
	}

	auto exact(const ca::usz size) noexcept -> char* {
		return static_cast<char*>(malloc(size == 0U ? 1U : size));
	}

	auto fill_from(char* const to, const ca::usz size, fuzz::input& in) noexcept -> void {
		const ca::span<const ca::u8> seed = in.bytes(64U);
		for (ca::usz i = 0U; i < size; ++i)
			to[i] = seed.size() == 0U ? static_cast<char>(i * 7U) : static_cast<char>(seed[i % seed.size()] + i / seed.size());
	}

	auto copies(fuzz::input& in) noexcept -> void {
		const ca::usz size = in.below(LARGEST);
		char* const from = exact(size);
		char* const to = exact(size);
		char* const model = exact(size);
		fill_from(from, size, in);
		fill_from(to, size, in);
		for (ca::usz i = 0U; i < size; ++i)
			model[i] = from[i];
		ca::copy_bytes(to, from, size);
		for (ca::usz i = 0U; i < size; ++i)
			fuzz::require(to[i] == model[i], "copy_bytes");
		free(from);
		free(to);
		free(model);
	}

	auto moves(fuzz::input& in) noexcept -> void {
		const ca::usz size = in.below(LARGEST) + 1U;
		const ca::usz from = in.below(size);
		const ca::usz to = in.below(size);
		const ca::usz count = in.below(size - (from > to ? from : to) + 1U);
		char* const data = exact(size);
		char* const model = exact(size);
		fill_from(data, size, in);
		for (ca::usz i = 0U; i < size; ++i)
			model[i] = data[i];
		if (to < from)
			for (ca::usz i = 0U; i < count; ++i)
				model[to + i] = model[from + i];
		else
			for (ca::usz i = count; i != 0U; --i)
				model[to + i - 1U] = model[from + i - 1U];
		ca::move_bytes(data + to, data + from, count);
		for (ca::usz i = 0U; i < size; ++i)
			fuzz::require(data[i] == model[i], "move_bytes");
		free(data);
		free(model);
	}

	auto fills(fuzz::input& in) noexcept -> void {
		const ca::usz size = in.below(LARGEST);
		const ca::usz at = in.below(size + 1U);
		const ca::usz count = in.below(size - at + 1U);
		const ca::u8 value = in.take<ca::u8>();
		char* const data = exact(size);
		fill_from(data, size, in);
		char* const model = exact(size);
		for (ca::usz i = 0U; i < size; ++i)
			model[i] = i >= at && i < at + count ? static_cast<char>(value) : data[i];
		ca::fill_bytes(data + at, value, count);
		for (ca::usz i = 0U; i < size; ++i)
			fuzz::require(data[i] == model[i], "fill_bytes");
		free(data);
		free(model);
	}

	auto compares(fuzz::input& in) noexcept -> void {
		const ca::usz size = in.below(LARGEST);
		char* const left = exact(size);
		char* const right = exact(size);
		fill_from(left, size, in);
		for (ca::usz i = 0U; i < size; ++i)
			right[i] = left[i];
		if (size != 0U && in.take<ca::u8>() % 4U != 0U)
			right[in.below(size)] ^= static_cast<char>(in.take<ca::u8>() | 1U);
		int model = 0;
		for (ca::usz i = 0U; i < size && model == 0; ++i)
			model = static_cast<int>(static_cast<unsigned char>(left[i])) - static_cast<int>(static_cast<unsigned char>(right[i]));
		fuzz::require(sign(ca::compare_bytes(left, right, size)) == sign(model), "compare_bytes");
		fuzz::require(sign(ca::compare_units(left, right, size)) == sign(model), "compare_units<char>");
		fuzz::require(ca::equal_units(left, right, size) == (model == 0), "equal_units<char>");
		free(left);
		free(right);
	}

	auto finds(fuzz::input& in) noexcept -> void {
		const ca::usz size = in.below(LARGEST);
		const char value = static_cast<char>(in.take<ca::u8>());
		char* const data = exact(size);
		fill_from(data, size, in);
		const char* model = nullptr;
		for (ca::usz i = 0U; i < size && model == nullptr; ++i)
			if (data[i] == value)
				model = data + i;
		fuzz::require(ca::find_byte(data, value, size) == model, "find_byte");
		fuzz::require(ca::find_unit(data, value, size) == model, "find_unit<char>");
		free(data);
	}

	auto lengths(fuzz::input& in) noexcept -> void {
		const ca::usz size = in.below(LARGEST) + 1U;
		char* const data = exact(size);
		fill_from(data, size, in);
		for (ca::usz i = 0U; i < size; ++i)
			if (data[i] == '\0')
				data[i] = 'x';
		const ca::usz end = in.below(size);
		data[end] = '\0';
		const ca::usz start = in.below(end + 1U);
		fuzz::require(ca::string_length(data + start) == end - start, "string_length");
		free(data);
	}

	template <typename C>
	auto wide(fuzz::input& in) noexcept -> void {
		const ca::usz count = in.below(LARGEST / sizeof(C));
		C* const left = static_cast<C*>(malloc((count == 0U ? 1U : count) * sizeof(C)));
		C* const right = static_cast<C*>(malloc((count == 0U ? 1U : count) * sizeof(C)));
		for (ca::usz i = 0U; i < count; ++i)
			left[i] = right[i] = static_cast<C>(in.take<ca::u8>() % 4U);
		if (count != 0U && in.take<ca::u8>() % 2U != 0U)
			right[in.below(count)] = static_cast<C>(in.take<ca::u32>());
		using unit = ca::uint_of<sizeof(C) * 8U>;
		int model = 0;
		for (ca::usz i = 0U; i < count && model == 0; ++i)
			model = static_cast<unit>(left[i]) < static_cast<unit>(right[i]) ? -1 : static_cast<unit>(left[i]) > static_cast<unit>(right[i]) ? 1 : 0;
		fuzz::require(sign(ca::compare_units(left, right, count)) == model, "compare_units<wide>");
		fuzz::require(ca::equal_units(left, right, count) == (model == 0), "equal_units<wide>");
		const C value = static_cast<C>(in.take<ca::u8>() % 4U);
		const C* found = nullptr;
		for (ca::usz i = 0U; i < count && found == nullptr; ++i)
			if (left[i] == value)
				found = left + i;
		fuzz::require(ca::find_unit(left, value, count) == found, "find_unit<wide>");
		free(left);
		free(right);
	}

} // namespace

extern "C" auto LLVMFuzzerTestOneInput(const ca::u8* const data, const ca::usz size) -> int {

	fuzz::input in{data, size};
	switch (in.take<ca::u8>() % 9U) {
	case 0: copies(in); break;
	case 1: moves(in); break;
	case 2: fills(in); break;
	case 3: compares(in); break;
	case 4: finds(in); break;
	case 5: lengths(in); break;
	case 6: wide<char16_t>(in); break;
	case 7: wide<char32_t>(in); break;
	default: wide<wchar_t>(in); break;
	}
	return 0;
}
