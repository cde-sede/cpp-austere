// ca::unicode on arbitrary UTF-8, UTF-16 and UTF-32: find_invalid against a decoder written
// from the definition, transcoded_size exact for transcode, every output well-formed, valid
// text surviving a round trip through each other encoding unchanged, invalid text sanitized
// with one U+FFFD per maximal invalid subpart as the reference decoder counts them, and a
// short output never splitting a code point.
#include "1-primitives/integers/u16.hpp"
#include "1-primitives/integers/u32.hpp"
#include "2-vocabulary/optional.hpp"
#include "2-vocabulary/sv/basic.hpp"
#include "2-vocabulary/unicode/decode.hpp"
#include "2-vocabulary/unicode/find_invalid.hpp"
#include "2-vocabulary/unicode/is_valid.hpp"
#include "2-vocabulary/unicode/transcode.hpp"
#include "2-vocabulary/unicode/transcoded_size.hpp"
#include "fuzz/fuzz.hpp"

namespace {

	constexpr ca::usz UNITS = 1024U;

	/* One code point of reference decoding: its value (U+FFFD when invalid) and its length. */
	struct step final {
			char32_t _code_point;
			ca::usz _length;
			bool _valid;
	}; // struct step

	auto reference(const char8_t* const text, const ca::usz size) noexcept -> step {
		const ca::u32 lead = text[0];
		if (lead < 0x80U)
			return step{lead, 1U, true};
		ca::usz length = 0U;
		ca::u32 low = 0x80U;
		ca::u32 high = 0xBFU;
		ca::u32 value = 0U;
		if (lead >= 0xC2U && lead <= 0xDFU) {
			length = 2U;
			value = lead & 0x1FU;
		}
		else if (lead >= 0xE0U && lead <= 0xEFU) {
			length = 3U;
			value = lead & 0x0FU;
			low = lead == 0xE0U ? 0xA0U : 0x80U;
			high = lead == 0xEDU ? 0x9FU : 0xBFU;
		}
		else if (lead >= 0xF0U && lead <= 0xF4U) {
			length = 4U;
			value = lead & 0x07U;
			low = lead == 0xF0U ? 0x90U : 0x80U;
			high = lead == 0xF4U ? 0x8FU : 0xBFU;
		}
		else
			return step{U'�', 1U, false};
		for (ca::usz i = 1U; i < length; ++i) {
			if (i >= size)
				return step{U'�', i, false};
			const ca::u32 unit = text[i];
			if (unit < (i == 1U ? low : 0x80U) || unit > (i == 1U ? high : 0xBFU))
				return step{U'�', i, false};
			value = (value << 6U) | (unit & 0x3FU);
		}
		return step{value, length, true};
	}

	auto reference(const char16_t* const text, const ca::usz size) noexcept -> step {
		const ca::u32 unit = text[0];
		if (unit < 0xD800U || unit > 0xDFFFU)
			return step{unit, 1U, true};
		if (unit >= 0xDC00U || size < 2U || text[1] < 0xDC00U || text[1] > 0xDFFFU)
			return step{U'�', 1U, false};
		return step{0x10000U + ((unit - 0xD800U) << 10U) + (static_cast<ca::u32>(text[1]) - 0xDC00U), 2U, true};
	}

	auto reference(const char32_t* const text, const ca::usz) noexcept -> step {
		const ca::u32 unit = text[0];
		if (unit > 0x10FFFFU || (unit >= 0xD800U && unit <= 0xDFFFU))
			return step{U'�', 1U, false};
		return step{unit, 1U, true};
	}

	template <typename C>
	struct decoded_text final {
			char32_t _code_points[UNITS];
			ca::usz _count;
			ca::usz _first_invalid;
	}; // struct decoded_text

	template <typename C>
	auto decode_all(const C* const text, const ca::usz size) noexcept -> decoded_text<C> {
		decoded_text<C> out{};
		out._first_invalid = ~ca::usz{0U};
		ca::usz at = 0U;
		while (at < size) {
			const step s = reference(text + at, size - at);
			if (!s._valid && out._first_invalid == ~ca::usz{0U})
				out._first_invalid = at;
			out._code_points[out._count++] = s._code_point;
			at += s._length;
		}
		return out;
	}

	template <typename To, typename From>
	auto through(const From* const text, const ca::usz size, const decoded_text<From>& model) noexcept -> void {
		const ca::sv::basic<From> view{text, size};
		const ca::usz needed = ca::unicode::transcoded_size<To>(view);
		static To out[UNITS * 4U];
		const ca::unicode::transcoded done = ca::unicode::transcode(view, out, needed);
		fuzz::require(done.read() == size && done.written() == needed, "transcoded_size is not what transcode writes");
		const ca::sv::basic<To> written{out, done.written()};
		fuzz::require(ca::unicode::is_valid(written), "transcode wrote ill-formed text");
		const decoded_text<To> again = decode_all(out, done.written());
		fuzz::require(again._count == model._count, "transcode changed the number of code points");
		for (ca::usz i = 0U; i < model._count; ++i)
			fuzz::require(again._code_points[i] == model._code_points[i], "transcode changed a code point");
		static From back[UNITS * 4U];
		const ca::unicode::transcoded returned = ca::unicode::transcode(written, back, ca::unicode::transcoded_size<From>(written));
		if (model._first_invalid == ~ca::usz{0U}) {
			fuzz::require(returned.written() == size, "round trip changed the length");
			for (ca::usz i = 0U; i < size; ++i)
				fuzz::require(back[i] == text[i], "round trip changed the text");
		}
		if (needed != 0U) {
			const ca::usz capacity = needed - 1U - (needed > 4U ? needed % 3U : 0U);
			const ca::unicode::transcoded partial = ca::unicode::transcode(view, out, capacity);
			fuzz::require(partial.written() <= capacity && partial.read() <= size, "transcode past its capacity");
			fuzz::require(ca::unicode::is_valid(ca::sv::basic<To>{out, partial.written()}), "a short transcode split a code point");
		}
	}

	template <typename C>
	auto check(fuzz::input& in) noexcept -> void {
		static C text[UNITS];
		ca::usz size = 0U;
		while (size < UNITS && in.left() >= sizeof(C)) {
			if constexpr (sizeof(C) == 4U) {
				const ca::u8 pick = in.take<ca::u8>();
				const ca::u32 raw = in.take<ca::u32>();
				text[size++] = static_cast<C>(pick < 200U ? raw % 0x11000U : pick < 230U ? 0xD800U + raw % 0x800U : raw);
			}
			else
				text[size++] = in.take<C>();
		}
		const decoded_text<C> model = decode_all(text, size);
		const ca::optional<ca::usz> invalid = ca::unicode::find_invalid(ca::sv::basic<C>{text, size});
		if (model._first_invalid == ~ca::usz{0U})
			fuzz::require(!invalid, "find_invalid on valid text");
		else
			fuzz::require(invalid && *invalid == model._first_invalid, "find_invalid at another place");
		if (size != 0U) {
			const auto first = ca::unicode::decode(ca::sv::basic<C>{text, size});
			const step s = reference(text, size);
			fuzz::require(first && first->code_point() == s._code_point && first->length() == s._length && first->valid() == s._valid, "decode differs");
		}
		through<char8_t>(text, size, model);
		through<char16_t>(text, size, model);
		through<char32_t>(text, size, model);
	}

} // namespace

extern "C" auto LLVMFuzzerTestOneInput(const ca::u8* const data, const ca::usz size) -> int {

	fuzz::input in{data, size};
	switch (in.take<ca::u8>() % 3U) {
	case 0: check<char8_t>(in); break;
	case 1: check<char16_t>(in); break;
	default: check<char32_t>(in); break;
	}
	return 0;
}
