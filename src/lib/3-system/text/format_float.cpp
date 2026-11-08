/* printf's %f %e %g %a (and capitals) without libc, digit for digit as glibc: every decimal
   digit of the value from impl::exact_decimal_of, rounded at the precision to nearest, ties to
   even on the exact value (glibc's default rounding mode); %a from the bits, nibbles rounded
   the same way, a carry making the leading digit 2 as glibc does. %g picks its style from the
   %e exponent after rounding to the precision, then rounds again in that style, as C requires. */

#include "1-primitives/config/architecture.hpp"
#include "1-primitives/floats/impl/exact_decimal.hpp"
#include "1-primitives/integers/i64.hpp"
#include "1-primitives/integers/u32.hpp"
#include "1-primitives/integers/u64.hpp"
#include "3-system/text/format_float.hpp"
#include "3-system/text/impl/float_text.hpp"

#if ca_target_feature(floats)


// -- CA NAMESPACE -----------------------------------------------------------

namespace ca {


	// -- FORMAT FLOAT -------------------------------------------------------

	namespace sys {


		namespace impl {

			/* @DOC An exact expansion rounded to some significant digits: _digits[0, _count),
			   value 0.d1 d2 ... * 10^_point. */
			struct rounded_float final {
					using self = ca::sys::impl::rounded_float;
					char _digits[ca::impl::exact_decimal::CAPACITY + 1U];
					ca::usz _count;
					ca::i32 _point;
			}; // struct rounded_float

		} // namespace impl

	} // namespace sys

} // namespace ca

/* exact rounded to keep significant digits (no digit when keep < 0, the value then rounding to
   zero), ties to even. A carry out of the first digit makes it 1 and moves the point. */
static auto round_to(const ca::impl::exact_decimal& exact, const ca::i64 keep, ca::sys::impl::rounded_float& out) noexcept -> void {

	out._point = exact._point;
	if (keep >= static_cast<ca::i64>(exact._count)) {
		for (ca::usz i = 0U; i < exact._count; ++i)
			out._digits[i] = exact._digits[i];
		out._count = exact._count;
		return;
	}
	if (keep < 0) {
		out._count = 0U;
		return;
	}
	const ca::usz kept = static_cast<ca::usz>(keep);
	const char dropped = exact._digits[kept];
	bool up = dropped > '5';
	if (dropped == '5') {
		bool rest = false;
		for (ca::usz i = kept + 1U; i < exact._count && !rest; ++i)
			rest = exact._digits[i] != '0';
		const bool odd = kept > 0U && ((exact._digits[kept - 1U] - '0') & 1) != 0;
		up = rest || odd;
	}
	for (ca::usz i = 0U; i < kept; ++i)
		out._digits[i] = exact._digits[i];
	out._count = kept;
	if (!up)
		return;
	ca::usz i = kept;
	while (i > 0U && out._digits[i - 1U] == '9')
		out._digits[--i] = '0';
	if (i > 0U) {
		++out._digits[i - 1U];
		return;
	}
	out._digits[0] = '1';
	if (out._count == 0U)
		out._count = 1U;
	++out._point;
}

static auto view(const ca::sys::impl::rounded_float& rounded) noexcept -> ca::sys::impl::float_digits {
	return ca::sys::impl::float_digits{rounded._digits, rounded._count, rounded._point};
}

static auto lower(const char conversion) noexcept -> char {
	return conversion >= 'A' && conversion <= 'Z' ? static_cast<char>(conversion - 'A' + 'a') : conversion;
}

static auto put_hexadecimal(ca::sys::impl::float_sink& sink, const ca::u64 bits, const ca::sint precision, const bool alternate, const bool upper) noexcept -> void {

	const char* const hex = upper ? "0123456789ABCDEF" : "0123456789abcdef";
	const ca::u32 field = static_cast<ca::u32>(bits >> 52U) & 0x7ffU;
	ca::u64 mantissa = bits & ((ca::u64{1} << 52U) - 1U);
	ca::u32 lead = field == 0U ? 0U : 1U;
	ca::i32 exponent = field == 0U ? (mantissa == 0U ? 0 : -1022) : static_cast<ca::i32>(field) - 1023;
	ca::i64 nibbles = 13;
	if (precision >= 0 && precision < 13) {
		const ca::u32 shift = static_cast<ca::u32>(13 - precision) * 4U;
		const ca::u64 dropped = mantissa & ((ca::u64{1} << shift) - 1U);
		ca::u64 kept = mantissa >> shift;
		const ca::u64 half = ca::u64{1} << (shift - 1U);
		const bool odd = precision > 0 ? (kept & 1U) != 0U : (lead & 1U) != 0U;
		if (dropped > half || (dropped == half && odd))
			++kept;
		if (precision == 0) {
			lead += static_cast<ca::u32>(kept);
			kept = 0U;
		} else if ((kept >> (static_cast<ca::u32>(precision) * 4U)) != 0U) {
			++lead;
			kept &= (ca::u64{1} << (static_cast<ca::u32>(precision) * 4U)) - 1U;
		}
		mantissa = kept << shift;
		 nibbles = precision;
	} else if (precision < 0) {
		nibbles = 13;
		while (nibbles > 0 && ((mantissa >> ((13 - nibbles) * 4)) & 0xfU) == 0U)
			--nibbles;
	}
	put(sink, '0');
	put(sink, upper ? 'X' : 'x');
	put(sink, hex[lead]);
	const ca::i64 printed = precision >= 0 ? static_cast<ca::i64>(precision) : nibbles;
	if (printed > 0 || alternate)
		put(sink, '.');
	for (ca::i64 i = 0; i < printed; ++i)
		put(sink, i < 13 ? hex[(mantissa >> ((12 - i) * 4)) & 0xfU] : '0');
	put(sink, upper ? 'P' : 'p');
	put(sink, exponent < 0 ? '-' : '+');
	const ca::i32 magnitude = exponent < 0 ? -exponent : exponent;
	char text[12];
	ca::usz length = 0U;
	for (ca::i32 rest = magnitude; rest != 0 || length == 0U; rest /= 10)
		text[length++] = static_cast<char>('0' + rest % 10);
	while (length != 0U)
		put(sink, text[--length]);
}

auto ca::sys::format_float(const ca::span<char> buffer, const ca::f64 value, const char conversion, const ca::sint precision, const bool alternate) noexcept -> ca::usz {

	if (buffer.size() == 0U) [[unlikely]]
		return 0U;
	ca::sys::impl::float_sink sink{buffer.data(), buffer.size(), 0U};
	const ca::u64 bits = __builtin_bit_cast(ca::u64, value);
	const bool negative = (bits >> 63U) != 0U;
	const bool upper = conversion >= 'A' && conversion <= 'Z';
	const char style = lower(conversion);
	if (value != value || value - value != 0.0) {
		ca::sys::impl::put_special(sink, negative, value == value, upper);
		return ca::sys::impl::finish(sink);
	}
	if (style == 'a') {
		if (negative)
			put(sink, '-');
		put_hexadecimal(sink, bits, precision, alternate, upper);
		return ca::sys::impl::finish(sink);
	}
	if (negative)
		put(sink, '-');
	const ca::i64 wanted = precision >= 0 ? static_cast<ca::i64>(precision) : 6;
	static_assert(sizeof(ca::impl::exact_decimal) + sizeof(ca::sys::impl::rounded_float) < 4096U);
	ca::impl::exact_decimal exact;
	if (value == 0.0) {
		exact._count = 0U;
		exact._point = 1;
	} else {
		ca::impl::exact_decimal_of(value, exact);
	}
	ca::sys::impl::rounded_float rounded;
	const char exponent_letter = upper ? 'E' : 'e';
	if (style == 'f') {
		round_to(exact, exact._point + wanted, rounded);
		ca::sys::impl::put_fixed(sink, view(rounded), wanted, alternate);
	} else if (style == 'e') {
		round_to(exact, wanted + 1, rounded);
		ca::sys::impl::put_scientific(sink, view(rounded), wanted, alternate, exponent_letter);
	} else {
		const ca::i64 significant = wanted == 0 ? 1 : wanted;
		round_to(exact, significant, rounded);
		const ca::i64 exponent = rounded._count == 0U ? 0 : rounded._point - 1;
		if (significant > exponent && exponent >= -4) {
			const ca::i64 fraction = significant - 1 - exponent;
			round_to(exact, exact._point + fraction, rounded);
			const ca::i64 shown = alternate ? fraction : ca::sys::impl::trimmed(view(rounded), rounded._point, fraction);
			ca::sys::impl::put_fixed(sink, view(rounded), shown, alternate);
		} else {
			const ca::i64 fraction = significant - 1;
			const ca::i64 shown = alternate ? fraction : ca::sys::impl::trimmed(view(rounded), 1, fraction);
			ca::sys::impl::put_scientific(sink, view(rounded), shown, alternate, exponent_letter);
		}
	}
	return ca::sys::impl::finish(sink);
}

#endif
