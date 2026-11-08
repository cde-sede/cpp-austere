/* @DOC The functions of libm whose result IEEE 754 fixes to the bit, without libm: what
   libca_runtime's fmod, floor, sqrt, ... are, and what compilers lower __builtin_fmod and the
   others to when no instruction does it. Each is a template instantiated for float, double and
   long double (x87 80-bit), behaving as glibc's: the same results, NaNs included, and errno set
   as glibc does (ca's last error). Transcendental functions (exp, sin, ...) are not exact and
   are not here. Linux x86-64 only, and absent from an integer-only build (no SSE2). */
#ifndef ca_system_math_impl_exact_math_hpp
#define ca_system_math_impl_exact_math_hpp

#include "1-primitives/integers/i64.hpp"


// -- CA NAMESPACE -----------------------------------------------------------

namespace ca {


	// -- SYS ----------------------------------------------------------------

	namespace sys {


		// -- EXACT MATH -----------------------------------------------------

		namespace impl {

			/* @DOC x - n y, n the quotient truncated: the x87 fprem loop, exact. errc::out_of_domain
			   for y zero or x infinite. */
			template <typename T>
			[[nodiscard]] auto fmod(T x, T y) noexcept -> T;

			/* @DOC x - n y, n the quotient rounded to even: the x87 fprem1 loop, exact.
			   errc::out_of_domain as fmod. */
			template <typename T>
			[[nodiscard]] auto remainder(T x, T y) noexcept -> T;

			/* @DOC remainder, and in quotient the sign and the 3 low bits of n. */
			template <typename T>
			[[nodiscard]] auto remquo(T x, T y, int* quotient) noexcept -> T;

			/* @DOC The square root, correctly rounded. errc::out_of_domain below zero. */
			template <typename T>
			[[nodiscard]] auto sqrt(T x) noexcept -> T;

			/* @DOC The integer below x, as T. */
			template <typename T>
			[[nodiscard]] auto floor(T x) noexcept -> T;

			/* @DOC The integer above x, as T. */
			template <typename T>
			[[nodiscard]] auto ceil(T x) noexcept -> T;

			/* @DOC x without its fraction. */
			template <typename T>
			[[nodiscard]] auto trunc(T x) noexcept -> T;

			/* @DOC The nearest integer, halves away from zero. */
			template <typename T>
			[[nodiscard]] auto round(T x) noexcept -> T;

			/* @DOC The nearest integer, halves to even. */
			template <typename T>
			[[nodiscard]] auto roundeven(T x) noexcept -> T;

			/* @DOC The integer x rounds to in the current rounding mode, inexact raised. */
			template <typename T>
			[[nodiscard]] auto rint(T x) noexcept -> T;

			/* @DOC rint, the floating-point exception flags left as they were. */
			template <typename T>
			[[nodiscard]] auto nearbyint(T x) noexcept -> T;

			/* @DOC rint as an integer; out of range or NaN, the integer indefinite (-2^63). */
			template <typename T>
			[[nodiscard]] auto lrint(T x) noexcept -> ca::i64;

			/* @DOC round as an integer; out of range or NaN as lrint. */
			template <typename T>
			[[nodiscard]] auto lround(T x) noexcept -> ca::i64;

			/* @DOC The smaller; a quiet NaN only when both are. */
			template <typename T>
			[[nodiscard]] auto fmin(T x, T y) noexcept -> T;

			/* @DOC The larger; a quiet NaN only when both are. */
			template <typename T>
			[[nodiscard]] auto fmax(T x, T y) noexcept -> T;

			/* @DOC x - y when positive, else zero. errc::out_of_range when it overflows. */
			template <typename T>
			[[nodiscard]] auto fdim(T x, T y) noexcept -> T;

			/* @DOC x y + z, rounded once: the FMA instruction when the CPU has it, else exact integer
			   arithmetic rounded to odd into long double, then to T. float and double. */
			template <typename T>
			[[nodiscard]] auto fma(T x, T y, T z) noexcept -> T;

			/* @DOC fma without the instruction, as on CPUs lacking it. */
			template <typename T>
			[[nodiscard]] auto fma_software(T x, T y, T z) noexcept -> T;

			/* @DOC x 2^n, rounded once: ldexp, scalbn and scalbln. errc::out_of_range when finite
			   nonzero x becomes zero or infinite. */
			template <typename T>
			[[nodiscard]] auto scalbln(T x, long n) noexcept -> T;

			/* @DOC x as m 2^e, |m| in [0.5, 1); e in exponent. */
			template <typename T>
			[[nodiscard]] auto frexp(T x, int* exponent) noexcept -> T;

			/* @DOC The fraction of x, signed as x; the integer part in whole. */
			template <typename T>
			[[nodiscard]] auto modf(T x, T* whole) noexcept -> T;

			/* @DOC The exponent of x as T, subnormals' true one; -inf for zero. */
			template <typename T>
			[[nodiscard]] auto logb(T x) noexcept -> T;

			/* @DOC The exponent of x as int; INT_MIN for zero and NaN, INT_MAX for infinities, each
			   errc::out_of_domain. */
			template <typename T>
			[[nodiscard]] auto ilogb(T x) noexcept -> int;

			/* @DOC The neighbor of x toward y. errc::out_of_range when it is infinite or subnormal. */
			template <typename T>
			[[nodiscard]] auto nextafter(T x, T y) noexcept -> T;

		} // namespace impl

	} // namespace sys

} // namespace ca

#endif // ca_system_math_impl_exact_math_hpp
