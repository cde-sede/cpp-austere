/* The shortest round-trip digits (Ryu, 1-primitives/floats/impl/shortest_decimal) laid out as
   %.Pg does, P = max(digits, 15) for f64 and max(digits, 6) for f32: what the former libc
   version printed by trying %.15g, %.16g, %.17g (%.6g ... %.9g) until strtod read the value
   back, since a correctly rounded %.Ng is the shortest form whenever N digits suffice. */

#include "1-primitives/config/architecture.hpp"
#include "1-primitives/floats/impl/shortest_decimal.hpp"
#include "1-primitives/integers/i64.hpp"
#include "1-primitives/integers/u32.hpp"
#include "1-primitives/integers/u64.hpp"
#include "3-system/text/format_float_shortest.hpp"
#include "3-system/text/impl/float_text.hpp"

#if ca_target_feature(floats)


static auto put_shortest(const ca::span<char> buffer, const bool negative, ca::impl::decimal_float decimal, const ca::i64 minimum) noexcept -> ca::usz {

	ca::sys::impl::float_sink sink{buffer.data(), buffer.size(), 0U};
	if (negative)
		put(sink, '-');
	while (decimal._digits % 10U == 0U) {
		decimal._digits /= 10U;
		++decimal._exponent;
	}
	char text[20];
	ca::usz count = 0U;
	for (ca::u64 rest = decimal._digits; rest != 0U; rest /= 10U)
		text[count++] = static_cast<char>('0' + rest % 10U);
	char digits[20];
	for (ca::usz i = 0U; i < count; ++i)
		digits[i] = text[count - 1U - i];
	const ca::sys::impl::float_digits view{digits, count, decimal._exponent + static_cast<ca::i32>(count)};
	const ca::i64 significant = static_cast<ca::i64>(count) > minimum ? static_cast<ca::i64>(count) : minimum;
	const ca::i64 exponent = view._point - 1;
	if (significant > exponent && exponent >= -4)
		ca::sys::impl::put_fixed(sink, view, ca::sys::impl::trimmed(view, view._point, significant - 1 - exponent), false);
	else
		ca::sys::impl::put_scientific(sink, view, ca::sys::impl::trimmed(view, 1, significant - 1), false, 'e');
	return ca::sys::impl::finish(sink);
}

static auto put_simple(const ca::span<char> buffer, const bool negative, const bool zero, const bool infinite) noexcept -> ca::usz {

	ca::sys::impl::float_sink sink{buffer.data(), buffer.size(), 0U};
	if (zero) {
		if (negative)
			put(sink, '-');
		put(sink, '0');
	} else {
		ca::sys::impl::put_special(sink, negative, infinite, false);
	}
	return ca::sys::impl::finish(sink);
}

auto ca::sys::format_float_shortest(const ca::span<char> buffer, const ca::f64 value) noexcept -> ca::usz {

	if (buffer.size() == 0U) [[unlikely]]
		return 0U;
	const bool negative = (__builtin_bit_cast(ca::u64, value) >> 63U) != 0U;
	if (value != value || value - value != 0.0 || value == 0.0)
		return put_simple(buffer, negative, value == 0.0, value == value);
	return put_shortest(buffer, negative, ca::impl::shortest_decimal(value), 15);
}

auto ca::sys::format_float_shortest(const ca::span<char> buffer, const ca::f32 value) noexcept -> ca::usz {

	if (buffer.size() == 0U) [[unlikely]]
		return 0U;
	const bool negative = (__builtin_bit_cast(ca::u32, value) >> 31U) != 0U;
	if (value != value || value - value != 0.0F || value == 0.0F)
		return put_simple(buffer, negative, value == 0.0F, value == value);
	return put_shortest(buffer, negative, ca::impl::shortest_decimal(value), 6);
}

#endif
