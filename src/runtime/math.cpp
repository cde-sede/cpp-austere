/* The exact functions of libm for programs linked without it: what __builtin_fmod,
   __builtin_floor (no SSE4.1), __builtin_sqrt (its errno path), __builtin_fmax and the others
   compile to calls of. ca::sys::impl's (math/impl/exact_math), the result and errno as glibc's.
   fmal and the transcendental functions are not provided. */

#include "1-primitives/config/architecture.hpp"
#include "1-primitives/config/platform.hpp"
#include "3-system/math/impl/exact_math.hpp"

#if ca_platform(linux) && ca_architecture(x86_64) && ca_target_feature(sse2)

extern "C" {

	auto fmod(const double x, const double y) noexcept -> double { // ns-leak: ignore
		return ca::sys::impl::fmod(x, y);
	}

	auto fmodf(const float x, const float y) noexcept -> float { // ns-leak: ignore
		return ca::sys::impl::fmod(x, y);
	}

	auto fmodl(const long double x, const long double y) noexcept -> long double { // ns-leak: ignore
		return ca::sys::impl::fmod(x, y);
	}

	auto remainder(const double x, const double y) noexcept -> double { // ns-leak: ignore
		return ca::sys::impl::remainder(x, y);
	}

	auto remainderf(const float x, const float y) noexcept -> float { // ns-leak: ignore
		return ca::sys::impl::remainder(x, y);
	}

	auto remainderl(const long double x, const long double y) noexcept -> long double { // ns-leak: ignore
		return ca::sys::impl::remainder(x, y);
	}

	auto remquo(const double x, const double y, int* const quotient) noexcept -> double { // ns-leak: ignore
		return ca::sys::impl::remquo(x, y, quotient);
	}

	auto remquof(const float x, const float y, int* const quotient) noexcept -> float { // ns-leak: ignore
		return ca::sys::impl::remquo(x, y, quotient);
	}

	auto remquol(const long double x, const long double y, int* const quotient) noexcept -> long double { // ns-leak: ignore
		return ca::sys::impl::remquo(x, y, quotient);
	}

	auto sqrt(const double x) noexcept -> double { // ns-leak: ignore
		return ca::sys::impl::sqrt(x);
	}

	auto sqrtf(const float x) noexcept -> float { // ns-leak: ignore
		return ca::sys::impl::sqrt(x);
	}

	auto sqrtl(const long double x) noexcept -> long double { // ns-leak: ignore
		return ca::sys::impl::sqrt(x);
	}

	auto floor(const double x) noexcept -> double { // ns-leak: ignore
		return ca::sys::impl::floor(x);
	}

	auto floorf(const float x) noexcept -> float { // ns-leak: ignore
		return ca::sys::impl::floor(x);
	}

	auto floorl(const long double x) noexcept -> long double { // ns-leak: ignore
		return ca::sys::impl::floor(x);
	}

	auto ceil(const double x) noexcept -> double { // ns-leak: ignore
		return ca::sys::impl::ceil(x);
	}

	auto ceilf(const float x) noexcept -> float { // ns-leak: ignore
		return ca::sys::impl::ceil(x);
	}

	auto ceill(const long double x) noexcept -> long double { // ns-leak: ignore
		return ca::sys::impl::ceil(x);
	}

	auto trunc(const double x) noexcept -> double { // ns-leak: ignore
		return ca::sys::impl::trunc(x);
	}

	auto truncf(const float x) noexcept -> float { // ns-leak: ignore
		return ca::sys::impl::trunc(x);
	}

	auto truncl(const long double x) noexcept -> long double { // ns-leak: ignore
		return ca::sys::impl::trunc(x);
	}

	auto round(const double x) noexcept -> double { // ns-leak: ignore
		return ca::sys::impl::round(x);
	}

	auto roundf(const float x) noexcept -> float { // ns-leak: ignore
		return ca::sys::impl::round(x);
	}

	auto roundl(const long double x) noexcept -> long double { // ns-leak: ignore
		return ca::sys::impl::round(x);
	}

	auto roundeven(const double x) noexcept -> double { // ns-leak: ignore
		return ca::sys::impl::roundeven(x);
	}

	auto roundevenf(const float x) noexcept -> float { // ns-leak: ignore
		return ca::sys::impl::roundeven(x);
	}

	auto roundevenl(const long double x) noexcept -> long double { // ns-leak: ignore
		return ca::sys::impl::roundeven(x);
	}

	auto rint(const double x) noexcept -> double { // ns-leak: ignore
		return ca::sys::impl::rint(x);
	}

	auto rintf(const float x) noexcept -> float { // ns-leak: ignore
		return ca::sys::impl::rint(x);
	}

	auto rintl(const long double x) noexcept -> long double { // ns-leak: ignore
		return ca::sys::impl::rint(x);
	}

	auto nearbyint(const double x) noexcept -> double { // ns-leak: ignore
		return ca::sys::impl::nearbyint(x);
	}

	auto nearbyintf(const float x) noexcept -> float { // ns-leak: ignore
		return ca::sys::impl::nearbyint(x);
	}

	auto nearbyintl(const long double x) noexcept -> long double { // ns-leak: ignore
		return ca::sys::impl::nearbyint(x);
	}

	auto lrint(const double x) noexcept -> long { // ns-leak: ignore
		return ca::sys::impl::lrint(x);
	}

	auto lrintf(const float x) noexcept -> long { // ns-leak: ignore
		return ca::sys::impl::lrint(x);
	}

	auto lrintl(const long double x) noexcept -> long { // ns-leak: ignore
		return ca::sys::impl::lrint(x);
	}

	auto llrint(const double x) noexcept -> long long { // ns-leak: ignore
		return ca::sys::impl::lrint(x);
	}

	auto llrintf(const float x) noexcept -> long long { // ns-leak: ignore
		return ca::sys::impl::lrint(x);
	}

	auto llrintl(const long double x) noexcept -> long long { // ns-leak: ignore
		return ca::sys::impl::lrint(x);
	}

	auto lround(const double x) noexcept -> long { // ns-leak: ignore
		return ca::sys::impl::lround(x);
	}

	auto lroundf(const float x) noexcept -> long { // ns-leak: ignore
		return ca::sys::impl::lround(x);
	}

	auto lroundl(const long double x) noexcept -> long { // ns-leak: ignore
		return ca::sys::impl::lround(x);
	}

	auto llround(const double x) noexcept -> long long { // ns-leak: ignore
		return ca::sys::impl::lround(x);
	}

	auto llroundf(const float x) noexcept -> long long { // ns-leak: ignore
		return ca::sys::impl::lround(x);
	}

	auto llroundl(const long double x) noexcept -> long long { // ns-leak: ignore
		return ca::sys::impl::lround(x);
	}

	auto fmin(const double x, const double y) noexcept -> double { // ns-leak: ignore
		return ca::sys::impl::fmin(x, y);
	}

	auto fminf(const float x, const float y) noexcept -> float { // ns-leak: ignore
		return ca::sys::impl::fmin(x, y);
	}

	auto fminl(const long double x, const long double y) noexcept -> long double { // ns-leak: ignore
		return ca::sys::impl::fmin(x, y);
	}

	auto fmax(const double x, const double y) noexcept -> double { // ns-leak: ignore
		return ca::sys::impl::fmax(x, y);
	}

	auto fmaxf(const float x, const float y) noexcept -> float { // ns-leak: ignore
		return ca::sys::impl::fmax(x, y);
	}

	auto fmaxl(const long double x, const long double y) noexcept -> long double { // ns-leak: ignore
		return ca::sys::impl::fmax(x, y);
	}

	auto fdim(const double x, const double y) noexcept -> double { // ns-leak: ignore
		return ca::sys::impl::fdim(x, y);
	}

	auto fdimf(const float x, const float y) noexcept -> float { // ns-leak: ignore
		return ca::sys::impl::fdim(x, y);
	}

	auto fdiml(const long double x, const long double y) noexcept -> long double { // ns-leak: ignore
		return ca::sys::impl::fdim(x, y);
	}

	auto fma(const double x, const double y, const double z) noexcept -> double { // ns-leak: ignore
		return ca::sys::impl::fma(x, y, z);
	}

	auto fmaf(const float x, const float y, const float z) noexcept -> float { // ns-leak: ignore
		return ca::sys::impl::fma(x, y, z);
	}

	auto ldexp(const double x, const int n) noexcept -> double { // ns-leak: ignore
		return ca::sys::impl::scalbln(x, n);
	}

	auto ldexpf(const float x, const int n) noexcept -> float { // ns-leak: ignore
		return ca::sys::impl::scalbln(x, n);
	}

	auto ldexpl(const long double x, const int n) noexcept -> long double { // ns-leak: ignore
		return ca::sys::impl::scalbln(x, n);
	}

	auto scalbn(const double x, const int n) noexcept -> double { // ns-leak: ignore
		return ca::sys::impl::scalbln(x, n);
	}

	auto scalbnf(const float x, const int n) noexcept -> float { // ns-leak: ignore
		return ca::sys::impl::scalbln(x, n);
	}

	auto scalbnl(const long double x, const int n) noexcept -> long double { // ns-leak: ignore
		return ca::sys::impl::scalbln(x, n);
	}

	auto scalbln(const double x, const long n) noexcept -> double { // ns-leak: ignore
		return ca::sys::impl::scalbln(x, n);
	}

	auto scalblnf(const float x, const long n) noexcept -> float { // ns-leak: ignore
		return ca::sys::impl::scalbln(x, n);
	}

	auto scalblnl(const long double x, const long n) noexcept -> long double { // ns-leak: ignore
		return ca::sys::impl::scalbln(x, n);
	}

	auto frexp(const double x, int* const exponent) noexcept -> double { // ns-leak: ignore
		return ca::sys::impl::frexp(x, exponent);
	}

	auto frexpf(const float x, int* const exponent) noexcept -> float { // ns-leak: ignore
		return ca::sys::impl::frexp(x, exponent);
	}

	auto frexpl(const long double x, int* const exponent) noexcept -> long double { // ns-leak: ignore
		return ca::sys::impl::frexp(x, exponent);
	}

	auto modf(const double x, double* const whole) noexcept -> double { // ns-leak: ignore
		return ca::sys::impl::modf(x, whole);
	}

	auto modff(const float x, float* const whole) noexcept -> float { // ns-leak: ignore
		return ca::sys::impl::modf(x, whole);
	}

	auto modfl(const long double x, long double* const whole) noexcept -> long double { // ns-leak: ignore
		return ca::sys::impl::modf(x, whole);
	}

	auto logb(const double x) noexcept -> double { // ns-leak: ignore
		return ca::sys::impl::logb(x);
	}

	auto logbf(const float x) noexcept -> float { // ns-leak: ignore
		return ca::sys::impl::logb(x);
	}

	auto logbl(const long double x) noexcept -> long double { // ns-leak: ignore
		return ca::sys::impl::logb(x);
	}

	auto ilogb(const double x) noexcept -> int { // ns-leak: ignore
		return ca::sys::impl::ilogb(x);
	}

	auto ilogbf(const float x) noexcept -> int { // ns-leak: ignore
		return ca::sys::impl::ilogb(x);
	}

	auto ilogbl(const long double x) noexcept -> int { // ns-leak: ignore
		return ca::sys::impl::ilogb(x);
	}

	auto nextafter(const double x, const double y) noexcept -> double { // ns-leak: ignore
		return ca::sys::impl::nextafter(x, y);
	}

	auto nextafterf(const float x, const float y) noexcept -> float { // ns-leak: ignore
		return ca::sys::impl::nextafter(x, y);
	}

	auto nextafterl(const long double x, const long double y) noexcept -> long double { // ns-leak: ignore
		return ca::sys::impl::nextafter(x, y);
	}

} // extern "C"

#endif
