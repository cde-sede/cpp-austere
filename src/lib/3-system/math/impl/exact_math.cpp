/* The exact functions of libm, on x86-64.

   1. HOW EACH IS DONE

   | function                       | float, double                    | long double             |
   |--------------------------------|----------------------------------|-------------------------|
   | fmod, remainder, remquo        | x87 fprem / fprem1 loop on the   | the same loop           |
   |                                | operands widened, exactly        |                         |
   | sqrt                           | sqrtss / sqrtsd                  | fsqrt                   |
   | floor, ceil, trunc, round,     | the fraction bits cleared, a     | frndint, the control    |
   | roundeven                      | unit carried in                  | word's rounding set     |
   | rint, nearbyint, lrint         | x + 2^p - 2^p in the MXCSR mode, | frndint, fistp          |
   |                                | cvtsd2si                         |                         |
   | ldexp, scalbn                  | one final multiply by 2^n        | fscale                  |
   | fma                            | vfmadd, else integers rounded to | -                       |
   |                                | odd into long double             |                         |
   | the others                     | the exponent and significand bits                          |

   fprem reduces x by y 2^k a part at a time, exactly: the remainder of a float or double,
   computed in long double, is the float or double remainder, as glibc's integer loops give.

   2. AS GLIBC

   Results equal glibc 2.39's bit for bit, NaNs included, in every rounding mode
   (tests/sys_exact_math_libc), and errno is set as its wrappers set it. Special operands go
   through the same operations as glibc's on the same values: (x y) / (x y) for fmod, (y x) /
   (y x) for remainder of double, the NaN sums of fmin, fmax and nextafter. Which NaN an SSE
   operation returns depends on the operand order, which compilers change for a written x + y:
   those are asm. fma without the instruction picks y's NaN, then x's, then z's, as
   vfmadd213sd does.

   One difference: a zero remainder has x's sign, as IEEE 754 says, where glibc's remainder of
   float and double rounding down or up, or of double for y below 2^-1020, can give the other
   one. */

#include "1-primitives/atomic/atomic.hpp"
#include "1-primitives/atomic/memory_order.hpp"
#include "1-primitives/bit/bit_cast.hpp"
#include "1-primitives/bit/countl_zero.hpp"
#include "1-primitives/config/architecture.hpp"
#include "1-primitives/config/platform.hpp"
#include "1-primitives/floats/f32.hpp"
#include "1-primitives/floats/f64.hpp"
#include "1-primitives/integers/i32.hpp"
#include "1-primitives/integers/u16.hpp"
#include "1-primitives/integers/u32.hpp"
#include "1-primitives/integers/u64.hpp"
#include "1-primitives/integers/u8.hpp"
#include "1-primitives/memory/impl/cpu_features.hpp"
#include "1-primitives/traits/is_same.hpp"
#include "3-system/error/errc.hpp"
#include "3-system/error/last_error.hpp"
#include "3-system/math/impl/exact_math.hpp"

#if ca_platform(linux) && ca_architecture(x86_64) && ca_target_feature(sse2)


// -- CA NAMESPACE -----------------------------------------------------------

namespace ca {


	// -- EXACT MATH ---------------------------------------------------------

	namespace impl {

		/* @DOC The bit layout of float and double. */
		template <typename T>
		struct exact_math_format;

		template <>
		struct exact_math_format<ca::f32> final {
				using self = ca::impl::exact_math_format<ca::f32>;
				using bits = ca::u32;
				static constexpr int MANTISSA = 23;
				static constexpr int BIAS = 127;
				static constexpr int TOP = 255;
		}; // struct exact_math_format

		template <>
		struct exact_math_format<ca::f64> final {
				using self = ca::impl::exact_math_format<ca::f64>;
				using bits = ca::u64;
				static constexpr int MANTISSA = 52;
				static constexpr int BIAS = 1023;
				static constexpr int TOP = 2047;
		}; // struct exact_math_format

		/* @DOC An x87 80-bit long double: the significand, its integer bit explicit, then the sign
		   and the 15-bit exponent, padded to 16 bytes. */
		struct x87_extended final {
				using self = ca::impl::x87_extended;
				ca::u64 _significand;
				ca::u16 _sign_exponent;
				ca::u16 _padding[3];
		}; // struct x87_extended

		/* @DOC The unsigned integer of float's or double's width. */
		template <typename T>
		using exact_math_bits = typename ca::impl::exact_math_format<T>::bits;

		/* @DOC The exact product of two significands, and sums of them. */
		__extension__ typedef unsigned __int128 exact_math_wide;

		/* @DOC What fnstenv stores: control, status and tag words, the last instruction and
		   operand. */
		struct x87_environment final {
				using self = ca::impl::x87_environment;
				ca::u32 _words[7];
		}; // struct x87_environment

		/* @DOC An odd, finite, nonzero value: _significand 2^_exponent, the significand's integer
		   bit explicit. */
		struct exact_math_unpacked final {
				using self = ca::impl::exact_math_unpacked;
				ca::u64 _significand;
				int _exponent;
				bool _negative;
		}; // struct exact_math_unpacked

	} // namespace impl

} // namespace ca

static_assert(sizeof(long double) == 16U && __LDBL_MANT_DIG__ == 64, "ca: long double must be x87 80-bit");
static_assert(sizeof(ca::impl::x87_extended) == sizeof(long double));

static constexpr int INT_LOWEST = -2147483647 - 1;
static constexpr int INT_HIGHEST = 2147483647;
static constexpr ca::u16 ROUND_NEAREST = 0x0000U;
static constexpr ca::u16 ROUND_DOWN = 0x0400U;
static constexpr ca::u16 ROUND_UP = 0x0800U;
static constexpr ca::u16 ROUND_ZERO = 0x0C00U;

template <typename T>
static constexpr bool IS_EXTENDED = ca::is_same<T, long double>;


// -- BITS -------------------------------------------------------------------

template <typename T>
static constexpr ca::impl::exact_math_bits<T> SIGN = static_cast<ca::impl::exact_math_bits<T>>(static_cast<ca::impl::exact_math_bits<T>>(1U) << (sizeof(T) * 8U - 1U));

template <typename T>
[[gnu::always_inline]] static inline auto to_bits(const T x) noexcept -> ca::impl::exact_math_bits<T> {
	return ca::bit_cast<ca::impl::exact_math_bits<T>>(x);
}

template <typename T>
[[gnu::always_inline]] static inline auto from_bits(const ca::impl::exact_math_bits<T> b) noexcept -> T {
	return ca::bit_cast<T>(b);
}

/* The unbiased exponent field of b: -BIAS for zeros and subnormals, TOP - BIAS for infinities
   and NaNs. */
template <typename T>
[[gnu::always_inline]] static inline auto field_of(const ca::impl::exact_math_bits<T> b) noexcept -> int {

	using format = ca::impl::exact_math_format<T>;
	return static_cast<int>(b >> format::MANTISSA & static_cast<ca::impl::exact_math_bits<T>>(format::TOP)) - format::BIAS;
}

[[gnu::always_inline]] static inline auto extended_of(const long double x) noexcept -> ca::impl::x87_extended {
	return ca::bit_cast<ca::impl::x87_extended>(x);
}

[[gnu::always_inline]] static inline auto long_double_of(const ca::impl::x87_extended e) noexcept -> long double {
	return ca::bit_cast<long double>(e);
}

template <typename T>
[[gnu::always_inline]] static inline auto is_nan(const T x) noexcept -> bool {
	return x != x;
}

template <typename T>
[[gnu::always_inline]] static inline auto is_signaling(const T x) noexcept -> bool {
	if constexpr (IS_EXTENDED<T>) {
		const ca::impl::x87_extended e = extended_of(x);
		return (e._sign_exponent & 0x7FFFU) == 0x7FFFU && (e._significand >> 62U & 1U) == 0U && (e._significand << 1U) != 0U;
	} else {
		using format = ca::impl::exact_math_format<T>;
		const ca::impl::exact_math_bits<T> magnitude = to_bits(x) & ~SIGN<T>;
		const ca::impl::exact_math_bits<T> infinity = static_cast<ca::impl::exact_math_bits<T>>(static_cast<ca::impl::exact_math_bits<T>>(format::TOP) << format::MANTISSA);
		const ca::impl::exact_math_bits<T> quiet = static_cast<ca::impl::exact_math_bits<T>>(static_cast<ca::impl::exact_math_bits<T>>(1U) << (format::MANTISSA - 1));
		return magnitude > infinity && (magnitude & quiet) == 0U;
	}
}

template <typename T>
[[gnu::always_inline]] static inline auto signed_zero(const T x) noexcept -> T {
	return __builtin_signbit(x) ? -T{0} : T{0};
}

template <typename T>
[[gnu::always_inline]] static inline auto fail(const T value, const ca::error error) noexcept -> T {

	ca::sys::set_last_error(error);
	return value;
}


// -- X87 --------------------------------------------------------------------

[[gnu::always_inline]] static inline auto partial_remainder(long double x, const long double y) noexcept -> long double {

	asm("1:\n\t"
	    "fprem\n\t"
	    "fnstsw %%ax\n\t"
	    "testw $0x400, %%ax\n\t"
	    "jnz 1b"
	    : "+t"(x) : "u"(y) : "ax", "cc");
	return x;
}

/* fprem1's remainder; in status, the status word of its last step, whose C0, C3 and C1 are the
   3 low bits of the quotient. */
[[gnu::always_inline]] static inline auto nearest_remainder(long double x, const long double y, ca::u16& status) noexcept -> long double {

	asm("1:\n\t"
	    "fprem1\n\t"
	    "fnstsw %%ax\n\t"
	    "testw $0x400, %%ax\n\t"
	    "jnz 1b"
	    : "+t"(x), "=a"(status) : "u"(y) : "cc");
	return x;
}

/* frndint with the control word's rounding set to rounding, then restored. */
static auto integral_x87(long double x, const ca::u16 rounding) noexcept -> long double {

	ca::u16 saved;
	asm volatile("fnstcw %0" : "=m"(saved));
	const ca::u16 changed = static_cast<ca::u16>((saved & ~ROUND_ZERO) | rounding);
	asm volatile("fldcw %1\n\t"
	             "frndint\n\t"
	             "fldcw %2"
	             : "+t"(x) : "m"(changed), "m"(saved));
	return x;
}


// -- INTEGRAL ---------------------------------------------------------------

/* x rounded to an integer toward zero (toward 0), -inf (-1) or +inf (1), from its bits. */
template <typename T>
static auto integral_bits(const T x, const int toward) noexcept -> T {

	using format = ca::impl::exact_math_format<T>;
	using U      = ca::impl::exact_math_bits<T>;
	const U b = to_bits(x);
	const int e = field_of<T>(b);
	if (e >= format::MANTISSA)
		return e == format::TOP - format::BIAS ? x + x : x;
	const bool negative = (b & SIGN<T>) != 0U;
	const bool away = (toward < 0 && negative) || (toward > 0 && !negative);
	if (e < 0) {
		if ((b & ~SIGN<T>) == 0U)
			return x;
		return away ? (negative ? T{-1} : T{1}) : signed_zero(x);
	}
	const U fraction = static_cast<U>((static_cast<U>(1U) << (format::MANTISSA - e)) - 1U);
	if ((b & fraction) == 0U)
		return x;
	return from_bits<T>(static_cast<U>((away ? b + fraction : b) & ~fraction));
}

/* x rounded to the nearest integer, halves to even or away from zero, from its bits. */
template <typename T>
static auto nearest_bits(const T x, const bool even) noexcept -> T {

	using format = ca::impl::exact_math_format<T>;
	using U      = ca::impl::exact_math_bits<T>;
	const U b = to_bits(x);
	const int e = field_of<T>(b);
	if (e >= format::MANTISSA)
		return e == format::TOP - format::BIAS ? x + x : x;
	const U sign = b & SIGN<T>;
	if (e < -1)
		return from_bits<T>(sign);
	if (e == -1) {
		if (even && (b & ~SIGN<T>) == to_bits(T{0.5}))
			return from_bits<T>(sign);
		return from_bits<T>(sign | to_bits(T{1}));
	}
	const U unit = static_cast<U>(static_cast<U>(1U) << (format::MANTISSA - e));
	const U fraction = static_cast<U>(unit - 1U);
	const U part = b & fraction;
	if (part == 0U)
		return x;
	const U half = unit >> 1U;
	const U whole = b & ~fraction;
	const bool up = part > half || (part == half && (!even || (whole & unit) != 0U));
	return from_bits<T>(up ? static_cast<U>(whole + unit) : whole);
}

/* x + 2^p - 2^p, p the significand's width: rounded in the MXCSR mode, the sign of x kept. */
template <typename T>
static auto rint_sse(const T x) noexcept -> T {

	using format = ca::impl::exact_math_format<T>;
	const ca::impl::exact_math_bits<T> b = to_bits(x);
	const int e = field_of<T>(b);
	if (e >= format::MANTISSA)
		return e == format::TOP - format::BIAS ? x + x : x;
	const T shift = static_cast<T>(static_cast<ca::impl::exact_math_bits<T>>(1U) << format::MANTISSA);
	const T signed_shift = (b & SIGN<T>) != 0U ? -shift : shift;
	const T r = (x + signed_shift) - signed_shift;
	return from_bits<T>((to_bits(r) & ~SIGN<T>) | (b & SIGN<T>));
}

template <typename T>
auto ca::sys::impl::floor(const T x) noexcept -> T {

	if constexpr (IS_EXTENDED<T>)
		return integral_x87(x, ROUND_DOWN);
	else
		return integral_bits(x, -1);
}

template <typename T>
auto ca::sys::impl::ceil(const T x) noexcept -> T {

	if constexpr (IS_EXTENDED<T>)
		return integral_x87(x, ROUND_UP);
	else
		return integral_bits(x, 1);
}

template <typename T>
auto ca::sys::impl::trunc(const T x) noexcept -> T {

	if constexpr (IS_EXTENDED<T>)
		return integral_x87(x, ROUND_ZERO);
	else
		return integral_bits(x, 0);
}

template <typename T>
auto ca::sys::impl::round(const T x) noexcept -> T {
	if constexpr (IS_EXTENDED<T>) {
		const long double whole = integral_x87(x, ROUND_ZERO);
		const long double part = x - whole;
		if (part >= 0.5L)
			return whole + 1.0L;
		if (part <= -0.5L)
			return whole - 1.0L;
		return whole;
	} else {
		return nearest_bits(x, false);
	}
}

template <typename T>
auto ca::sys::impl::roundeven(const T x) noexcept -> T {

	if constexpr (IS_EXTENDED<T>)
		return integral_x87(x, ROUND_NEAREST);
	else
		return nearest_bits(x, true);
}

template <typename T>
auto ca::sys::impl::rint(T x) noexcept -> T {
	if constexpr (IS_EXTENDED<T>) {
		asm("frndint" : "+t"(x));
		return x;
	} else {
		return rint_sse(x);
	}
}

template <typename T>
auto ca::sys::impl::nearbyint(T x) noexcept -> T {
	if constexpr (IS_EXTENDED<T>) {
		ca::impl::x87_environment saved;
		asm volatile("fnstenv %1\n\t"
		             "frndint\n\t"
		             "fldenv %1"
		             : "+t"(x), "+m"(saved));
		return x;
	} else {
		ca::u32 control;
		asm volatile("stmxcsr %1" : "+x"(x), "=m"(control));
		T r = rint_sse(x);
		asm volatile("ldmxcsr %1" : "+x"(r) : "m"(control));
		return r;
	}
}

template <typename T>
auto ca::sys::impl::lrint(const T x) noexcept -> ca::i64 {

	ca::i64 r;
	if constexpr (IS_EXTENDED<T>)
		asm("fistpll %0" : "=m"(r) : "t"(x) : "st");
	else if constexpr (sizeof(T) == 4U)
		asm("cvtss2si %1, %0" : "=r"(r) : "x"(x));
	else
		asm("cvtsd2si %1, %0" : "=r"(r) : "x"(x));
	return r;
}

template <typename T>
auto ca::sys::impl::lround(const T x) noexcept -> ca::i64 {

	const T whole = ca::sys::impl::round(x);
	ca::i64 r;
	if constexpr (IS_EXTENDED<T>)
		asm("fistpll %0" : "=m"(r) : "t"(whole) : "st");
	else if constexpr (sizeof(T) == 4U)
		asm("cvttss2si %1, %0" : "=r"(r) : "x"(whole));
	else
		asm("cvttsd2si %1, %0" : "=r"(r) : "x"(whole));
	return r;
}


// -- REMAINDERS -------------------------------------------------------------

/* (a b) / (a b) in that order, a's NaN chosen over b's: glibc's result for special operands. */
template <typename T>
static auto special_quotient(T a, const T b) noexcept -> T {

	if constexpr (sizeof(T) == 4U)
		asm("mulss %1, %0\n\t"
		    "divss %0, %0"
		    : "+x"(a) : "x"(b));
	else
		asm("mulsd %1, %0\n\t"
		    "divsd %0, %0"
		    : "+x"(a) : "x"(b));
	return a;
}

template <typename T>
[[gnu::always_inline]] static inline auto remainder_domain(const T x, const T y) noexcept -> bool {
	return (y == T{0} || __builtin_isinf(x)) && !is_nan(x) && !is_nan(y);
}

template <typename T>
auto ca::sys::impl::fmod(const T x, const T y) noexcept -> T {
	if constexpr (IS_EXTENDED<T>) {
		if (remainder_domain(x, y))
			ca::sys::set_last_error(ca::errc::out_of_domain);
		return partial_remainder(x, y);
	} else {
		if (y == T{0} || !__builtin_isfinite(x) || is_nan(y)) [[unlikely]] {
			const T r = special_quotient(x, y);
			return remainder_domain(x, y) ? fail(r, ca::errc::out_of_domain) : r;
		}
		return static_cast<T>(partial_remainder(x, y));
	}
}

template <typename T>
auto ca::sys::impl::remainder(const T x, const T y) noexcept -> T {

	if (remainder_domain(x, y))
		ca::sys::set_last_error(ca::errc::out_of_domain);
	ca::u16 status;
	if constexpr (IS_EXTENDED<T>) {
		return nearest_remainder(x, y, status);
	} else {
		if (y == T{0} || !__builtin_isfinite(x) || is_nan(y)) [[unlikely]]
			return sizeof(T) == 4U ? special_quotient(x, y) : special_quotient(y, x);
		return static_cast<T>(nearest_remainder(x, y, status));
	}
}

template <typename T>
auto ca::sys::impl::remquo(const T x, const T y, int* const quotient) noexcept -> T {

	ca::u16 status;
	if (y == T{0} || !__builtin_isfinite(x) || is_nan(y)) [[unlikely]] {
		if constexpr (IS_EXTENDED<T>)
			return nearest_remainder(x, y, status);
		else
			return special_quotient(x, y);
	}
	const long double r = nearest_remainder(x, y, status);
	int low = (status >> 8U & 1U) << 2U | (status >> 14U & 1U) << 1U | (status >> 9U & 1U);
	if (low == 0 && r != 0.0L && (__builtin_signbit(r) != 0) != (__builtin_signbit(x) != 0))
		low = 8;
	*quotient = (__builtin_signbit(x) != 0) != (__builtin_signbit(y) != 0) ? -low : low;
	return static_cast<T>(r);
}


// -- SQRT -------------------------------------------------------------------

template <typename T>
auto ca::sys::impl::sqrt(const T x) noexcept -> T {

	T r;
	if constexpr (IS_EXTENDED<T>)
		asm("fsqrt" : "=t"(r) : "0"(x));
	else if constexpr (sizeof(T) == 4U)
		asm("sqrtss %1, %0" : "=x"(r) : "x"(x));
	else
		asm("sqrtsd %1, %0" : "=x"(r) : "x"(x));
	return __builtin_isless(x, T{0}) ? fail(r, ca::errc::out_of_domain) : r;
}


// -- MIN AND MAX ------------------------------------------------------------

/* a + b in that order, a's NaN chosen over b's: compilers commute a written a + b. */
template <typename T>
static auto ordered_sum(T a, const T b) noexcept -> T {

	if constexpr (sizeof(T) == 4U)
		asm("addss %1, %0" : "+x"(a) : "x"(b));
	else
		asm("addsd %1, %0" : "+x"(a) : "x"(b));
	return a;
}

/* y + x on the x87 (faddp, y in st(1)): of two NaNs, the larger significand. */
static auto x87_sum(const long double y, const long double x) noexcept -> long double {

	long double r;
	asm("faddp" : "=t"(r) : "0"(x), "u"(y) : "st(1)");
	return r;
}

/* fmin and fmax with a NaN, as glibc's: a quiet NaN is ignored; else the NaNs added (SSE: x's
   first; x87: the larger significand). */
template <typename T>
static auto unordered_choice(const T x, const T y) noexcept -> T {
	if constexpr (IS_EXTENDED<T>) {
		if (!is_nan(y))
			return is_signaling(x) ? x87_sum(x, y) : y;
		if (!is_nan(x))
			return is_signaling(y) ? x87_sum(y, x) : x;
		return x87_sum(y, x);
	} else {
		if (is_signaling(x) || is_signaling(y))
			return ordered_sum(x, y);
		return is_nan(y) ? x : y;
	}
}

/* Equal operands: SSE's minsd and maxsd give y, glibc's x87 fminl x and fmaxl y. */
template <typename T>
auto ca::sys::impl::fmin(const T x, const T y) noexcept -> T {

	if (IS_EXTENDED<T> ? __builtin_islessequal(x, y) : __builtin_isless(x, y))
		return x;
	if (!is_nan(x) && !is_nan(y))
		return y;
	return unordered_choice(x, y);
}

template <typename T>
auto ca::sys::impl::fmax(const T x, const T y) noexcept -> T {

	if (__builtin_isgreater(x, y))
		return x;
	if (!is_nan(x) && !is_nan(y))
		return y;
	return unordered_choice(x, y);
}

template <typename T>
auto ca::sys::impl::fdim(const T x, const T y) noexcept -> T {

	if (__builtin_islessequal(x, y))
		return T{0};
	const T r = x - y;
	if (__builtin_isinf(r) && !__builtin_isinf(x) && !__builtin_isinf(y))
		return fail(r, ca::errc::out_of_range);
	return r;
}


// -- FMA --------------------------------------------------------------------

static constinit ca::atomic<ca::u8> fma_instruction{0U}; // file-state: ignore

static auto has_fma_instruction(void) noexcept -> bool {

	ca::u8 known = fma_instruction.load<ca::memory_order::relaxed>();
	if (known == 0U) [[unlikely]] {
		known = ca::impl::read_cpu_features()._fma ? 1U : 2U;
		fma_instruction.store<ca::memory_order::relaxed>(known);
	}
	return known == 1U;
}

template <typename T>
static auto unpack(const T x) noexcept -> ca::impl::exact_math_unpacked {

	using format = ca::impl::exact_math_format<T>;
	const ca::impl::exact_math_bits<T> b = to_bits(x);
	const int e = field_of<T>(b);
	const ca::u64 mantissa = b & ((static_cast<ca::impl::exact_math_bits<T>>(1U) << format::MANTISSA) - 1U);
	if (e == -format::BIAS)
		return {mantissa, 1 - format::BIAS - format::MANTISSA, (b & SIGN<T>) != 0U};
	return {mantissa | static_cast<ca::u64>(1U) << format::MANTISSA, e - format::MANTISSA, (b & SIGN<T>) != 0U};
}

static auto leading_zeros(const ca::impl::exact_math_wide v) noexcept -> int {

	const ca::u64 high = static_cast<ca::u64>(v >> 64U);
	return high != 0U ? ca::countl_zero(high) : 64 + ca::countl_zero(static_cast<ca::u64>(v));
}

/* +0 + -0 at run time: -0 when rounding down, +0 otherwise, as an exact zero sum. */
template <typename T>
static auto zero_sum(void) noexcept -> T {

	T positive = T{0};
	T negative = -T{0};
	asm("" : "+x"(positive), "+x"(negative));
	return positive + negative;
}

/* x y + z for finite nonzero x, y and z: the exact sum in 128 bits, rounded to odd at 64 bits
   into a long double (the low bits as a sticky bit), then to T once, which is exact rounding
   since 64 >= 2 + T's precision. */
template <typename T>
static auto fused_exact(const T x, const T y, const T z) noexcept -> T {

	const ca::impl::exact_math_unpacked ux = unpack(x);
	const ca::impl::exact_math_unpacked uy = unpack(y);
	const ca::impl::exact_math_unpacked uz = unpack(z);
	ca::impl::exact_math_wide p = static_cast<ca::impl::exact_math_wide>(ux._significand) * uy._significand;
	const int shift_p = leading_zeros(p);
	p <<= shift_p;
	const int ep = ux._exponent + uy._exponent - shift_p;
	const bool sp = ux._negative != uy._negative;
	const int shift_q = ca::countl_zero(uz._significand);
	const ca::impl::exact_math_wide q = static_cast<ca::impl::exact_math_wide>(uz._significand) << (64 + shift_q);
	const int eq = uz._exponent - 64 - shift_q;
	const bool p_larger = ep > eq || (ep == eq && p >= q);
	ca::impl::exact_math_wide a = p_larger ? p : q;
	ca::impl::exact_math_wide b = p_larger ? q : p;
	const int ea = p_larger ? ep : eq;
	const int eb = p_larger ? eq : ep;
	const bool negative = p_larger ? sp : uz._negative;
	const bool subtract = sp != uz._negative;
	a >>= 1U;
	const int shift = ea - eb + 1;
	if (shift >= 128) {
		b = 1U;
	} else {
		const bool lost = (b & ((static_cast<ca::impl::exact_math_wide>(1U) << shift) - 1U)) != 0U;
		b = b >> shift | static_cast<ca::impl::exact_math_wide>(lost);
	}
	ca::impl::exact_math_wide sum = subtract ? a - b : a + b;
	if (sum == 0U)
		return zero_sum<T>();
	const int shift_sum = leading_zeros(sum);
	sum <<= shift_sum;
	const ca::u64 high = static_cast<ca::u64>(sum >> 64U) | static_cast<ca::u64>(static_cast<ca::u64>(sum) != 0U);
	const int exponent = ea + 1 - shift_sum + 64;
	ca::impl::x87_extended e{};
	  e._significand = high;
	e._sign_exponent = static_cast<ca::u16>((negative ? 0x8000U : 0U) | static_cast<ca::u32>(exponent + 63 + 16383));
	return static_cast<T>(long_double_of(e));
}

template <typename T>
auto ca::sys::impl::fma(T x, const T y, const T z) noexcept -> T {

	if (!has_fma_instruction()) [[unlikely]]
		return ca::sys::impl::fma_software(x, y, z);
	if constexpr (sizeof(T) == 4U)
		asm("vfmadd213ss %2, %1, %0" : "+x"(x) : "x"(y), "x"(z));
	else
		asm("vfmadd213sd %2, %1, %0" : "+x"(x) : "x"(y), "x"(z));
	return x;
}

template <typename T>
auto ca::sys::impl::fma_software(const T x, const T y, const T z) noexcept -> T {

	static_assert(!IS_EXTENDED<T>, "ca: fma of long double is not provided");
	if (is_nan(y))
		return y + y;
	if (is_nan(x))
		return x + x;
	if (is_nan(z))
		return z + z;
	if (!__builtin_isfinite(x) || !__builtin_isfinite(y)) {
		const T product = x * y;
		return product + z;
	}
	if (__builtin_isinf(z))
		return z;
	if (x == T{0} || y == T{0}) {
		const T product = x * y;
		return product + z;
	}
	if (z == T{0})
		return x * y;
	return fused_exact(x, y, z);
}


// -- SCALING ----------------------------------------------------------------

template <typename T>
static auto scale(const T x, long n) noexcept -> T {
	if constexpr (IS_EXTENDED<T>) {
		long double r;
		asm("fscale" : "=t"(r) : "0"(x), "u"(static_cast<long double>(n)));
		return r;
	} else {
		using format = ca::impl::exact_math_format<T>;
		using U      = ca::impl::exact_math_bits<T>;
		constexpr long highest = format::BIAS;
		constexpr long lowest = 1 - format::BIAS;
		constexpr long step = format::BIAS - 1 - (format::MANTISSA + 1);
		const T up = from_bits<T>(static_cast<U>(static_cast<U>(2 * format::BIAS) << format::MANTISSA));
		const T down = from_bits<T>(static_cast<U>(static_cast<U>(format::MANTISSA + 2) << format::MANTISSA));
		T y = x;
		if (n > highest) {
			y *= up;
			n -= highest;
			if (n > highest) {
				y *= up;
				n -= highest;
				if (n > highest)
					n = highest;
			}
		} else if (n < lowest) {
			y *= down;
			n += step;
			if (n < lowest) {
				y *= down;
				n += step;
				if (n < lowest)
					n = lowest;
			}
		}
		return y * from_bits<T>(static_cast<U>(static_cast<U>(format::BIAS + n) << format::MANTISSA));
	}
}

template <typename T>
auto ca::sys::impl::scalbln(const T x, const long n) noexcept -> T {

	if (!__builtin_isfinite(x) || x == T{0})
		return x + x;
	const T r = scale(x, n);
	if (!__builtin_isfinite(r) || r == T{0})
		return fail(r, ca::errc::out_of_range);
	return r;
}


// -- PARTS ------------------------------------------------------------------

template <typename T>
auto ca::sys::impl::frexp(const T x, int* const exponent) noexcept -> T {

	*exponent = 0;
	if constexpr (IS_EXTENDED<T>) {
		ca::impl::x87_extended e = extended_of(x);
		int field = e._sign_exponent & 0x7FFF;
		if (field == 0x7FFF || (field == 0 && e._significand == 0U))
			return x + x;
		int adjust = 0;
		if (field == 0) {
			     e = extended_of(x * 0x1p65L);
			 field = e._sign_exponent & 0x7FFF;
			adjust = -65;
		}
		*exponent = field - 16382 + adjust;
		e._sign_exponent = static_cast<ca::u16>((e._sign_exponent & 0x8000U) | 0x3FFEU);
		return long_double_of(e);
	} else {
		using format = ca::impl::exact_math_format<T>;
		using U      = ca::impl::exact_math_bits<T>;
		constexpr U exponent_bits = static_cast<U>(static_cast<U>(format::TOP) << format::MANTISSA);
		U b = to_bits(x);
		if ((b & exponent_bits) == exponent_bits || (b & ~SIGN<T>) == 0U)
			return x + x;
		int adjust = 0;
		if ((b & exponent_bits) == 0U) {
			const T scale = from_bits<T>(static_cast<U>(static_cast<U>(format::BIAS + format::MANTISSA + 2) << format::MANTISSA));
			     b = to_bits(static_cast<T>(x * scale));
			adjust = -(format::MANTISSA + 2);
		}
		*exponent = field_of<T>(b) + 1 + adjust;
		return from_bits<T>(static_cast<U>((b & ~exponent_bits) | static_cast<U>(static_cast<U>(format::BIAS - 1) << format::MANTISSA)));
	}
}

template <typename T>
auto ca::sys::impl::modf(const T x, T* const whole) noexcept -> T {

	const T integer = ca::sys::impl::trunc(x);
	*whole = integer;
	if (__builtin_isinf(x) || x == integer)
		return signed_zero(x);
	return x - integer;
}

template <typename T>
auto ca::sys::impl::logb(const T x) noexcept -> T {
	if constexpr (IS_EXTENDED<T>) {
		long double r;
		asm("fxtract\n\t"
		    "fstp %%st(0)"
		    : "=t"(r) : "0"(x));
		return r;
	} else {
		using format = ca::impl::exact_math_format<T>;
		using U      = ca::impl::exact_math_bits<T>;
		const U magnitude = to_bits(x) & ~SIGN<T>;
		if (magnitude == 0U)
			return T{-1} / __builtin_fabs(x);
		const int e = field_of<T>(magnitude);
		if (e == format::TOP - format::BIAS)
			return x * x;
		if (e == -format::BIAS)
			return static_cast<T>(1 - format::BIAS - format::MANTISSA + static_cast<int>(sizeof(U) * 8U) - 1 - ca::countl_zero(magnitude));
		return static_cast<T>(e);
	}
}

template <typename T>
auto ca::sys::impl::ilogb(const T x) noexcept -> int {
	if constexpr (IS_EXTENDED<T>) {
		const ca::impl::x87_extended e = extended_of(x);
		const int field = e._sign_exponent & 0x7FFF;
		if (field == 0x7FFF)
			return fail((e._significand << 1U) == 0U ? INT_HIGHEST : INT_LOWEST, ca::errc::out_of_domain);
		if (field == 0) {
			if (e._significand == 0U)
				return fail(INT_LOWEST, ca::errc::out_of_domain);
			return -16382 - ca::countl_zero(e._significand);
		}
		return field - 16383;
	} else {
		using format = ca::impl::exact_math_format<T>;
		using U      = ca::impl::exact_math_bits<T>;
		const U magnitude = to_bits(x) & ~SIGN<T>;
		if (magnitude == 0U)
			return fail(INT_LOWEST, ca::errc::out_of_domain);
		const int e = field_of<T>(magnitude);
		if (e == format::TOP - format::BIAS) {
			const U infinity = static_cast<U>(static_cast<U>(format::TOP) << format::MANTISSA);
			return fail(magnitude == infinity ? INT_HIGHEST : INT_LOWEST, ca::errc::out_of_domain);
		}
		if (e == -format::BIAS)
			return 1 - format::BIAS - format::MANTISSA + static_cast<int>(sizeof(U) * 8U) - 1 - ca::countl_zero(magnitude);
		return e;
	}
}

template <typename T>
auto ca::sys::impl::nextafter(const T x, const T y) noexcept -> T {

	if (is_nan(x) || is_nan(y)) {
		if constexpr (IS_EXTENDED<T>)
			return x87_sum(y, x);
		else
			return ordered_sum(y, x);
	}
	if (x == y)
		return y;
	const bool away = (x < y) != (__builtin_signbit(x) != 0);
	if constexpr (IS_EXTENDED<T>) {
		ca::impl::x87_extended e = extended_of(x);
		constexpr ca::u64 integer_bit = static_cast<ca::u64>(1U) << 63U;
		if (x == 0.0L) {
			  e._significand = 1U;
			e._sign_exponent = __builtin_signbit(y) ? 0x8000U : 0U;
			return long_double_of(e);
		}
		if (away) {
			++e._significand;
			if (e._significand == 0U) {
				e._significand = integer_bit;
				++e._sign_exponent;
			} else if ((e._sign_exponent & 0x7FFFU) == 0U && e._significand == integer_bit) {
				++e._sign_exponent;
			}
		} else if (e._significand == integer_bit && (e._sign_exponent & 0x7FFFU) != 0U) {
			--e._sign_exponent;
			e._significand = (e._sign_exponent & 0x7FFFU) == 0U ? integer_bit - 1U : ~static_cast<ca::u64>(0U);
		} else {
			--e._significand;
		}
		const int field = e._sign_exponent & 0x7FFF;
		const long double r = long_double_of(e);
		return field == 0x7FFF || field == 0 ? fail(r, ca::errc::out_of_range) : r;
	} else {
		using U      = ca::impl::exact_math_bits<T>;
		U b = to_bits(x);
		if ((b & ~SIGN<T>) == 0U)
			return from_bits<T>(static_cast<U>((to_bits(y) & SIGN<T>) | 1U));
		if (away)
			++b;
		else
			--b;
		const int e = field_of<T>(b);
		using format = ca::impl::exact_math_format<T>;
		const T r = from_bits<T>(b);
		return e == format::TOP - format::BIAS || e == -format::BIAS ? fail(r, ca::errc::out_of_range) : r;
	}
}


// -- INSTANTIATIONS ---------------------------------------------------------

template auto ca::sys::impl::fmod(ca::f32, ca::f32) noexcept -> ca::f32;
template auto ca::sys::impl::fmod(ca::f64, ca::f64) noexcept -> ca::f64;
template auto ca::sys::impl::fmod(long double, long double) noexcept -> long double;
template auto ca::sys::impl::remainder(ca::f32, ca::f32) noexcept -> ca::f32;
template auto ca::sys::impl::remainder(ca::f64, ca::f64) noexcept -> ca::f64;
template auto ca::sys::impl::remainder(long double, long double) noexcept -> long double;
template auto ca::sys::impl::remquo(ca::f32, ca::f32, int*) noexcept -> ca::f32;
template auto ca::sys::impl::remquo(ca::f64, ca::f64, int*) noexcept -> ca::f64;
template auto ca::sys::impl::remquo(long double, long double, int*) noexcept -> long double;
template auto ca::sys::impl::sqrt(ca::f32) noexcept -> ca::f32;
template auto ca::sys::impl::sqrt(ca::f64) noexcept -> ca::f64;
template auto ca::sys::impl::sqrt(long double) noexcept -> long double;
template auto ca::sys::impl::floor(ca::f32) noexcept -> ca::f32;
template auto ca::sys::impl::floor(ca::f64) noexcept -> ca::f64;
template auto ca::sys::impl::floor(long double) noexcept -> long double;
template auto ca::sys::impl::ceil(ca::f32) noexcept -> ca::f32;
template auto ca::sys::impl::ceil(ca::f64) noexcept -> ca::f64;
template auto ca::sys::impl::ceil(long double) noexcept -> long double;
template auto ca::sys::impl::trunc(ca::f32) noexcept -> ca::f32;
template auto ca::sys::impl::trunc(ca::f64) noexcept -> ca::f64;
template auto ca::sys::impl::trunc(long double) noexcept -> long double;
template auto ca::sys::impl::round(ca::f32) noexcept -> ca::f32;
template auto ca::sys::impl::round(ca::f64) noexcept -> ca::f64;
template auto ca::sys::impl::round(long double) noexcept -> long double;
template auto ca::sys::impl::roundeven(ca::f32) noexcept -> ca::f32;
template auto ca::sys::impl::roundeven(ca::f64) noexcept -> ca::f64;
template auto ca::sys::impl::roundeven(long double) noexcept -> long double;
template auto ca::sys::impl::rint(ca::f32) noexcept -> ca::f32;
template auto ca::sys::impl::rint(ca::f64) noexcept -> ca::f64;
template auto ca::sys::impl::rint(long double) noexcept -> long double;
template auto ca::sys::impl::nearbyint(ca::f32) noexcept -> ca::f32;
template auto ca::sys::impl::nearbyint(ca::f64) noexcept -> ca::f64;
template auto ca::sys::impl::nearbyint(long double) noexcept -> long double;
template auto ca::sys::impl::lrint(ca::f32) noexcept -> ca::i64;
template auto ca::sys::impl::lrint(ca::f64) noexcept -> ca::i64;
template auto ca::sys::impl::lrint(long double) noexcept -> ca::i64;
template auto ca::sys::impl::lround(ca::f32) noexcept -> ca::i64;
template auto ca::sys::impl::lround(ca::f64) noexcept -> ca::i64;
template auto ca::sys::impl::lround(long double) noexcept -> ca::i64;
template auto ca::sys::impl::fmin(ca::f32, ca::f32) noexcept -> ca::f32;
template auto ca::sys::impl::fmin(ca::f64, ca::f64) noexcept -> ca::f64;
template auto ca::sys::impl::fmin(long double, long double) noexcept -> long double;
template auto ca::sys::impl::fmax(ca::f32, ca::f32) noexcept -> ca::f32;
template auto ca::sys::impl::fmax(ca::f64, ca::f64) noexcept -> ca::f64;
template auto ca::sys::impl::fmax(long double, long double) noexcept -> long double;
template auto ca::sys::impl::fdim(ca::f32, ca::f32) noexcept -> ca::f32;
template auto ca::sys::impl::fdim(ca::f64, ca::f64) noexcept -> ca::f64;
template auto ca::sys::impl::fdim(long double, long double) noexcept -> long double;
template auto ca::sys::impl::fma(ca::f32, ca::f32, ca::f32) noexcept -> ca::f32;
template auto ca::sys::impl::fma(ca::f64, ca::f64, ca::f64) noexcept -> ca::f64;
template auto ca::sys::impl::fma_software(ca::f32, ca::f32, ca::f32) noexcept -> ca::f32;
template auto ca::sys::impl::fma_software(ca::f64, ca::f64, ca::f64) noexcept -> ca::f64;
template auto ca::sys::impl::scalbln(ca::f32, long) noexcept -> ca::f32;
template auto ca::sys::impl::scalbln(ca::f64, long) noexcept -> ca::f64;
template auto ca::sys::impl::scalbln(long double, long) noexcept -> long double;
template auto ca::sys::impl::frexp(ca::f32, int*) noexcept -> ca::f32;
template auto ca::sys::impl::frexp(ca::f64, int*) noexcept -> ca::f64;
template auto ca::sys::impl::frexp(long double, int*) noexcept -> long double;
template auto ca::sys::impl::modf(ca::f32, ca::f32*) noexcept -> ca::f32;
template auto ca::sys::impl::modf(ca::f64, ca::f64*) noexcept -> ca::f64;
template auto ca::sys::impl::modf(long double, long double*) noexcept -> long double;
template auto ca::sys::impl::logb(ca::f32) noexcept -> ca::f32;
template auto ca::sys::impl::logb(ca::f64) noexcept -> ca::f64;
template auto ca::sys::impl::logb(long double) noexcept -> long double;
template auto ca::sys::impl::ilogb(ca::f32) noexcept -> int;
template auto ca::sys::impl::ilogb(ca::f64) noexcept -> int;
template auto ca::sys::impl::ilogb(long double) noexcept -> int;
template auto ca::sys::impl::nextafter(ca::f32, ca::f32) noexcept -> ca::f32;
template auto ca::sys::impl::nextafter(ca::f64, ca::f64) noexcept -> ca::f64;
template auto ca::sys::impl::nextafter(long double, long double) noexcept -> long double;

#endif
