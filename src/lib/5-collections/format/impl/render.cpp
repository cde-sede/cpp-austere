#include "1-primitives/config/architecture.hpp"
#include "1-primitives/floats/f32.hpp"
#include "1-primitives/floats/f64.hpp"
#include "1-primitives/integers/i64.hpp"
#include "1-primitives/integers/u16.hpp"
#include "1-primitives/integers/u32.hpp"
#include "1-primitives/integers/u64.hpp"
#include "1-primitives/integers/u8.hpp"
#include "1-primitives/integers/uint_of.hpp"
#include "1-primitives/integers/usz.hpp"
#include "1-primitives/numeric/min.hpp"
#include "2-vocabulary/error.hpp"
#include "2-vocabulary/sv/basic.hpp"
#include "2-vocabulary/sv/str.hpp"
#include "2-vocabulary/unicode/decoded.hpp"
#include "2-vocabulary/unicode/encode.hpp"
#include "2-vocabulary/unicode/impl/decode_at.hpp"
#include "2-vocabulary/unicode/impl/skip_ascii.hpp"
#include "2-vocabulary/unicode/is_scalar_value.hpp"
#include "2-vocabulary/unicode/transcode.hpp"
#include "3-system/error/error_message.hpp"
#include "3-system/error/error_name.hpp"
#include "3-system/text/format_float.hpp"
#include "3-system/text/format_float_shortest.hpp"
#include "3-system/trace/of.hpp"
#include "3-system/trace/record.hpp"
#include "5-collections/format/impl/display_kind.hpp"
#include "5-collections/format/impl/render.hpp"


static constexpr char digit_pairs[] =
	"00010203040506070809101112131415161718192021222324252627282930313233343536373839"
	"40414243444546474849505152535455565758596061626364656667686970717273747576777879"
	"8081828384858687888990919293949596979899";
static constexpr char lower_digits[] = "0123456789abcdef";
static constexpr char upper_digits[] = "0123456789ABCDEF";
static constexpr ca::usz no_limit = ~ca::usz{0U};
static constexpr ca::usz transcode_capacity = 256U;


// -- CA NAMESPACE -----------------------------------------------------------

namespace ca {


	// -- RENDER -------------------------------------------------------------

	namespace impl {

		/* @DOC A prefix of text: its code units and code points. */
		struct text_extent final {

				using self = ca::impl::text_extent;

				ca::usz _units;
				ca::usz _points;

		}; // struct text_extent

		/* @DOC Escaped text sent to a writer. */
		struct escape_writer final {

				using self = ca::impl::escape_writer;

				ca::text_writer* _out;

				auto put(const char* const data, const ca::usz size, const ca::usz) noexcept -> void {
					_out->put(ca::sv::str{data, size});
				}

		}; // struct escape_writer

		/* @DOC Escaped text counted in code points, for padding. */
		struct escape_counter final {

				using self = ca::impl::escape_counter;

				ca::usz _points;

				auto put(const char* const, const ca::usz, const ca::usz points) noexcept -> void {
					_points += points;
				}

		}; // struct escape_counter

		/* @DOC One escaped character, quotes included, kept to be padded. */
		struct escape_buffer final {

				using self = ca::impl::escape_buffer;

				char _text[32];
				ca::usz _size;
				ca::usz _points;

				auto put(const char* const data, const ca::usz size, const ca::usz points) noexcept -> void {

					for (ca::usz i = 0U; i < size; ++i)
						_text[_size + i] = data[i];
					  _size += size;
					_points += points;
				}

		}; // struct escape_buffer

	} // namespace impl

} // namespace ca


[[nodiscard]] static auto to_decimal(char* end, ca::u64 value) noexcept -> char* {

	while (value >= 100U) {
		const ca::u64 pair = (value % 100U) * 2U;
		value /= 100U;
		*--end = digit_pairs[pair + 1U];
		*--end = digit_pairs[pair];
	}
	if (value >= 10U) {
		*--end = digit_pairs[value * 2U + 1U];
		*--end = digit_pairs[value * 2U];
	}
	else
		*--end = static_cast<char>('0' + value);
	return end;
}

[[nodiscard]] static auto to_power_of_two(char* end, ca::u64 value, const unsigned shift, const char* const digits) noexcept -> char* {

	const ca::u64 mask = (ca::u64{1U} << shift) - 1U;
	do {
		*--end = digits[value & mask];
		value >>= shift;
	} while (value != 0U);
	return end;
}

static auto emit(ca::text_writer& out, const ca::impl::format_spec& spec, const char default_align, const char* const prefix, const ca::usz prefix_size, const ca::usz zeros, const char* const body, const ca::usz body_size, const ca::usz body_width, const bool numeric) noexcept -> void {

	const ca::usz size = prefix_size + zeros + body_width;
	const ca::usz pad = spec._width > size ? spec._width - size : 0U;
	if (pad == 0U || (numeric && spec._zero && spec._align == '\0')) {
		out.put(ca::sv::str{prefix, prefix_size});
		out.fill('0', zeros + pad);
		out.put(ca::sv::str{body, body_size});
		return;
	}
	const char align = spec._align != '\0' ? spec._align : default_align;
	const ca::usz before = align == '<' ? 0U : align == '^' ? pad / 2U : pad;
	out.fill(spec._fill, before);
	out.put(ca::sv::str{prefix, prefix_size});
	out.fill('0', zeros);
	out.put(ca::sv::str{body, body_size});
	out.fill(spec._fill, pad - before);
}

static auto render_integer(ca::text_writer& out, const ca::impl::format_spec& spec, const bool negative, const ca::u64 magnitude) noexcept -> void {

	char digits[64];
	char* const end = digits + sizeof(digits);
	char* begin = end;
	char prefix[3];
	ca::usz prefix_size = 0U;
	if (negative)
		prefix[prefix_size++] = '-';
	else if (spec._sign != '\0')
		prefix[prefix_size++] = spec._sign;
	if (magnitude != 0U || spec._precision != 0) {
		switch (spec._type) {
			case 'x':
				begin = to_power_of_two(end, magnitude, 4U, lower_digits);
				break;
			case 'X':
				begin = to_power_of_two(end, magnitude, 4U, upper_digits);
				break;
			case 'o':
				begin = to_power_of_two(end, magnitude, 3U, lower_digits);
				break;
			case 'b': case 'B':
				begin = to_power_of_two(end, magnitude, 1U, lower_digits);
				break;
			default:
				begin = to_decimal(end, magnitude);
				break;
		}
	}
	if (spec._alternate) {
		if (spec._type == 'o') {
			if (begin == end || *begin != '0')
				prefix[prefix_size++] = '0';
		}
		else if (spec._type == 'x' || spec._type == 'X' || spec._type == 'b' || spec._type == 'B') {
			prefix[prefix_size++] = '0';
			prefix[prefix_size++] = spec._type;
		}
	}
	const ca::usz count = static_cast<ca::usz>(end - begin);
	const ca::usz minimum = spec._precision > 0 ? static_cast<ca::usz>(spec._precision) : 0U;
	emit(out, spec, '>', prefix, prefix_size, minimum > count ? minimum - count : 0U, begin, count, count, true);
}

static auto render_signed(ca::text_writer& out, const ca::impl::format_spec& spec, const ca::i64 value, const ca::u8 size) noexcept -> void {

	if (value >= 0) {
		render_integer(out, spec, false, static_cast<ca::u64>(value));
		return;
	}
	if (spec._wrap) {
		const ca::u64 mask = size >= 8U ? ~ca::u64{0U} : (ca::u64{1U} << (size * 8U)) - 1U;
		render_integer(out, spec, false, static_cast<ca::u64>(value) & mask);
		return;
	}
	render_integer(out, spec, true, ca::u64{0U} - static_cast<ca::u64>(value));
}

template <typename C>
[[nodiscard]] static auto measure(const C* const data, const ca::usz size, const ca::usz limit) noexcept -> ca::impl::text_extent {

	if constexpr (sizeof(C) == 1U) {
		const ca::usz room = size < limit ? size : limit;
		if (ca::unicode::impl::is_ascii(data, room)) [[likely]]
			return ca::impl::text_extent{room, room};
	}
	const C* const end = data + size;
	const C* at = data;
	ca::usz points = 0U;
	while (at != end && points < limit) {
		if constexpr (sizeof(C) == 1U) {
			const ca::usz run = ca::min(static_cast<ca::usz>(ca::unicode::impl::skip_ascii(at, end) - at), limit - points);
			    at += run;
			points += run;
			if (at == end || points == limit)
				break;
		}
		at += ca::unicode::impl::decode_at(at, end).length();
		++points;
	}
	return ca::impl::text_extent{static_cast<ca::usz>(at - data), points};
}

[[nodiscard]] static auto limit_of(const ca::impl::format_spec& spec) noexcept -> ca::usz {
	return spec._precision >= 0 ? static_cast<ca::usz>(spec._precision) : no_limit;
}

[[nodiscard]] static auto padding_before(const ca::impl::format_spec& spec, const ca::usz pad) noexcept -> ca::usz {

	const char align = spec._align != '\0' ? spec._align : '<';
	return align == '<' ? 0U : align == '^' ? pad / 2U : pad;
}

static auto render_text(ca::text_writer& out, const ca::impl::format_spec& spec, const char* const data, const ca::usz size) noexcept -> void {

	if (spec._width == 0U && spec._precision < 0) {
		out.put(ca::sv::str{data, size});
		return;
	}
	const ca::impl::text_extent text = measure(data, size, limit_of(spec));
	emit(out, spec, '<', nullptr, 0U, 0U, data, text._units, text._points, false);
}

template <typename Sink>
static auto put_hex_escape(Sink& sink, const char kind, const ca::u32 value) noexcept -> void {

	char digits[8];
	char* const end = digits + sizeof(digits);
	const char* const begin = to_power_of_two(end, value, 4U, lower_digits);
	char text[16] = {'\\', kind, '{'};
	ca::usz size = 3U;
	for (const char* at = begin; at != end; ++at)
		text[size++] = *at;
	text[size++] = '}';
	sink.put(text, size, size);
}

template <typename Sink>
static auto put_escaped_point(Sink& sink, const char32_t point, const char quote) noexcept -> void {

	char simple = '\0';
	if (point == U'\t')
		simple = 't';
	else if (point == U'\n')
		simple = 'n';
	else if (point == U'\r')
		simple = 'r';
	else if (point == U'\0')
		simple = '0';
	else if (point == U'\\' || point == static_cast<char32_t>(quote))
		simple = static_cast<char>(point);
	if (simple != '\0') {
		const char text[2] = {'\\', simple};
		sink.put(text, 2U, 2U);
		return;
	}
	if (point < U' ' || (point >= U'\x7F' && point <= U'\x9F')) {
		put_hex_escape(sink, 'u', static_cast<ca::u32>(point));
		return;
	}
	char units[4];
	sink.put(units, ca::unicode::encode(point, units), 1U);
}

/* @DOC text between quotes, escaped: \t \n \r \0 \\ and the quote, other controls as \u{..},
   invalid UTF-8 bytes as \x{..}, invalid UTF-16/32 units as \u{..}. */
template <typename C, typename Sink>
static auto put_escaped(Sink& sink, const C* const data, const ca::usz size, const char quote) noexcept -> void {

	const C* const end = data + size;
	const C* at = data;
	sink.put(&quote, 1U, 1U);
	while (at != end) {
		const ca::unicode::decoded point = ca::unicode::impl::decode_at(at, end);
		if (point.valid())
			put_escaped_point(sink, point.code_point(), quote);
		else {
			for (ca::usz i = 0U; i < point.length(); ++i)
				put_hex_escape(sink, sizeof(C) == 1U ? 'x' : 'u', static_cast<ca::u32>(static_cast<ca::uint_of<sizeof(C) * 8U>>(at[i])));
		}
		at += point.length();
	}
	sink.put(&quote, 1U, 1U);
}

template <typename C>
static auto render_quoted(ca::text_writer& out, const ca::impl::format_spec& spec, const C* const data, const ca::usz size, const char quote) noexcept -> void {

	const ca::usz units = spec._precision >= 0 ? measure(data, size, limit_of(spec))._units : size;
	ca::impl::escape_writer writer{&out};
	if (spec._width == 0U) {
		put_escaped(writer, data, units, quote);
		return;
	}
	ca::impl::escape_counter counter{0U};
	put_escaped(counter, data, units, quote);
	const ca::usz pad = spec._width > counter._points ? spec._width - counter._points : 0U;
	const ca::usz before = padding_before(spec, pad);
	out.fill(spec._fill, before);
	put_escaped(writer, data, units, quote);
	out.fill(spec._fill, pad - before);
}

static auto render_quoted_buffer(ca::text_writer& out, const ca::impl::format_spec& spec, const ca::impl::escape_buffer& buffer) noexcept -> void {
	emit(out, spec, '<', nullptr, 0U, 0U, buffer._text, buffer._size, buffer._points, false);
}

template <typename C>
static auto put_transcoded(ca::text_writer& out, const C* const data, const ca::usz size) noexcept -> void {

	char buffer[transcode_capacity];
	ca::sv::basic<C> rest{data, size};
	while (!rest.empty()) {
		const ca::unicode::transcoded done = ca::unicode::transcode(rest, buffer, transcode_capacity);
		out.put(ca::sv::str{buffer, done.written()});
		rest = rest.without_prefix(done.read());
	}
}

template <typename C>
static auto render_wide(ca::text_writer& out, const ca::impl::format_spec& spec, const C* const data, const ca::usz size) noexcept -> void {

	if (spec._type == '?') {
		render_quoted(out, spec, data, size, '"');
		return;
	}
	if (spec._width == 0U && spec._precision < 0) {
		put_transcoded(out, data, size);
		return;
	}
	const ca::impl::text_extent text = measure(data, size, limit_of(spec));
	const ca::usz pad = spec._width > text._points ? spec._width - text._points : 0U;
	const ca::usz before = padding_before(spec, pad);
	out.fill(spec._fill, before);
	put_transcoded(out, data, text._units);
	out.fill(spec._fill, pad - before);
}

static auto render_code_point(ca::text_writer& out, const ca::impl::format_spec& spec, const ca::u32 value, const ca::u8 size) noexcept -> void {

	const bool text = spec._type == '\0' || spec._type == 'c' || spec._type == 's' || spec._type == '?';
	if (!text) {
		render_integer(out, spec, false, value);
		return;
	}
	const bool valid = size == 2U ? value < 0xD800U || value > 0xDFFFU : ca::unicode::is_scalar_value(static_cast<char32_t>(value));
	if (spec._type == '?') {
		ca::impl::escape_buffer buffer{{}, 0U, 0U};
		buffer.put("'", 1U, 1U);
		if (valid)
			put_escaped_point(buffer, static_cast<char32_t>(value), '\'');
		else
			put_hex_escape(buffer, 'u', value);
		buffer.put("'", 1U, 1U);
		render_quoted_buffer(out, spec, buffer);
		return;
	}
	char units[4];
	render_text(out, spec, units, ca::unicode::encode(static_cast<char32_t>(value), units));
}

static auto render_pointer(ca::text_writer& out, const ca::impl::format_spec& spec, const void* const pointer) noexcept -> void {

	char digits[16];
	char* const end = digits + sizeof(digits);
	char* const begin = to_power_of_two(end, reinterpret_cast<ca::usz>(pointer), 4U, lower_digits);
	emit(out, spec, '>', "0x", 2U, 0U, begin, static_cast<ca::usz>(end - begin), static_cast<ca::usz>(end - begin), true);
}

static auto error_text(const ca::error error) noexcept -> ca::sv::str {

	const ca::trace::record record = ca::trace::of(error);
	if (!record.frames().empty() && !record.frames()[0].message().empty())
		return record.frames()[0].message();
	const ca::sv::str name = ca::sys::error_name(error);
	if (!name.empty())
		return name;
	return ca::sys::error_message(error);
}

#if ca_target_feature(floats)

static constexpr ca::usz float_capacity = 512U;

static auto render_float(ca::text_writer& out, const ca::impl::format_spec& spec, const ca::f64 value, const ca::u8 size) noexcept -> void {

	char text[float_capacity];
	ca::usz length = 0U;
	if (spec._type == '\0' && spec._precision < 0)
		length = size == sizeof(ca::f32) ? ca::sys::format_float_shortest(text, static_cast<ca::f32>(value)) : ca::sys::format_float_shortest(text, value);
	else
		length = ca::sys::format_float(text, value, spec._type != '\0' ? spec._type : 'g', spec._precision, spec._alternate);
	const char* body = text;
	char sign = spec._sign;
	if (length != 0U && body[0] == '-') {
		sign = '-';
		++body;
		--length;
	}
	const bool numeric = length != 0U && body[0] >= '0' && body[0] <= '9';
	emit(out, spec, '>', &sign, sign != '\0' ? 1U : 0U, 0U, body, length, length, numeric);
}

#endif

static auto render_argument(ca::text_writer& out, const ca::impl::format_spec& spec, const ca::impl::display_argument& argument) noexcept -> void {
	switch (argument._kind) {
		case ca::impl::display_kind::boolean:
			if (spec._type == '\0' || spec._type == 's')
				render_text(out, spec, argument._boolean ? "true" : "false", argument._boolean ? 4U : 5U);
			else
				render_integer(out, spec, false, argument._boolean ? 1U : 0U);
			break;
		case ca::impl::display_kind::character:
			if (spec._type == '?') {
				ca::impl::escape_buffer buffer{{}, 0U, 0U};
				put_escaped(buffer, &argument._character, 1U, '\'');
				render_quoted_buffer(out, spec, buffer);
			}
			else if (spec._type == '\0' || spec._type == 'c' || spec._type == 's')
				render_text(out, spec, &argument._character, 1U);
			else
				render_signed(out, spec, argument._character, 1U);
			break;
		case ca::impl::display_kind::code_point:
			render_code_point(out, spec, static_cast<ca::u32>(argument._unsigned_value), argument._size);
			break;
		case ca::impl::display_kind::signed_integer:
			if (spec._type == 'c') {
				const char c = static_cast<char>(argument._signed_value);
				render_text(out, spec, &c, 1U);
			}
			else
				render_signed(out, spec, argument._signed_value, argument._size);
			break;
		case ca::impl::display_kind::unsigned_integer:
			if (spec._type == 'c') {
				const char c = static_cast<char>(argument._unsigned_value);
				render_text(out, spec, &c, 1U);
			}
			else
				render_integer(out, spec, false, argument._unsigned_value);
			break;
		case ca::impl::display_kind::floating:
#if ca_target_feature(floats)
			render_float(out, spec, argument._float_value, argument._size);
#endif
			break;
		case ca::impl::display_kind::string:
			if (spec._type == 'p')
				render_pointer(out, spec, argument._text._data);
			else if (argument._text._data == nullptr)
				render_text(out, spec, "(null)", 6U);
			else if (spec._type == '?')
				render_quoted(out, spec, argument._text._data, argument._text._size, '"');
			else
				render_text(out, spec, argument._text._data, argument._text._size);
			break;
		case ca::impl::display_kind::wide_string:
			if (spec._type == 'p')
				render_pointer(out, spec, argument._units._data);
			else if (argument._units._data == nullptr)
				render_text(out, spec, "(null)", 6U);
			else if ((argument._size & ca::impl::wide_text_flag) != 0U)
				render_wide(out, spec, static_cast<const wchar_t*>(argument._units._data), argument._units._size);
			else if (argument._size == 2U)
				render_wide(out, spec, static_cast<const char16_t*>(argument._units._data), argument._units._size);
			else
				render_wide(out, spec, static_cast<const char32_t*>(argument._units._data), argument._units._size);
			break;
		case ca::impl::display_kind::pointer:
			render_pointer(out, spec, argument._pointer);
			break;
		case ca::impl::display_kind::error:
			if (spec._type == 'd')
				render_signed(out, spec, argument._error._code, sizeof(int));
			else {
				const ca::sv::str message = error_text(ca::error{argument._error._code, argument._error._trace});
				render_text(out, spec, message.data(), message.size());
			}
			break;
		case ca::impl::display_kind::custom:
			argument._custom._display(argument._custom._object, out);
			break;
		case ca::impl::display_kind::none:
			break;
	}
}

static auto render_piece(ca::text_writer& out, const char* const text, const ca::impl::format_piece& piece) noexcept -> void {

	const char* at = text + piece._offset;
	const char* const end = at + piece._size;
	if (piece._escape == '\0') {
		out.put(ca::sv::str{at, piece._size});
		return;
	}
	const char* run = at;
	while (at < end) {
		const char c = *at;
		const bool doubled = piece._escape == '%' ? c == '%' : c == '{' || c == '}';
		if (doubled) {
			out.put(ca::sv::str{run, static_cast<ca::usz>(at - run) + 1U});
			at += 2;
			run = at;
		}
		else
			++at;
	}
	out.put(ca::sv::str{run, static_cast<ca::usz>(end - run)});
}

auto ca::impl::render(ca::text_writer& out, const char* const text, const ca::impl::format_piece* const pieces, const ca::impl::format_spec* const specs, const ca::impl::display_argument* const arguments, const ca::usz count) noexcept -> void {

	for (ca::usz i = 0U; i < count; ++i) {
		render_piece(out, text, pieces[i]);
		render_argument(out, specs[i], arguments[i]);
	}
	render_piece(out, text, pieces[count]);
}
