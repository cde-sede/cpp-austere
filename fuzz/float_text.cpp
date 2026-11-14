// ca's float text against glibc: sys::format_float digit for digit with snprintf for every
// conversion (f F e E g G a A), precision and # flag; sys::format_float_shortest against the
// smallest %.Ng that strtod reads back (f64, N 15 to 17) or strtof (f32, N 6 to 9). The two
// differences tests/sys_format_float_libc.cpp documents are left out: %#g, and the shortest
// form of subnormals, which is checked to read back instead. A third, found here: at a power of
// two the interval that reads back is lopsided, the correctly rounded %.Ng can fall outside it
// while another string of N digits is inside; ca's output is then shorter than glibc's, which
// is accepted when it reads back (0x1p-1007: ca 7.291122019556398e-304, glibc's
// 7.2911220195563975e-304).
#include "1-primitives/floats/f32.hpp"
#include "1-primitives/floats/f64.hpp"
#include "1-primitives/integers/u64.hpp"
#include "3-system/text/format_float.hpp"
#include "3-system/text/format_float_shortest.hpp"
#include "fuzz/fuzz.hpp"

extern "C" auto snprintf(char* out, ca::usz size, const char* format, ...) noexcept -> int;
extern "C" auto strtod(const char* text, char** end) noexcept -> double;
extern "C" auto strtof(const char* text, char** end) noexcept -> float;
extern "C" auto strcmp(const char* a, const char* b) noexcept -> int;

namespace {

	constexpr char CONVERSIONS[] = "feEgGaAF";
	constexpr double SMALLEST_NORMAL = 2.2250738585072014e-308;
	constexpr float SMALLEST_NORMAL_F32 = 1.17549435e-38F;

	char want[2048];
	char got[2048];

	auto check_conversion(const double value, const char conversion, const int precision, const bool alternate) noexcept -> void {
		char format[8];
		int at = 0;
		format[at++] = '%';
		if (alternate)
			format[at++] = '#';
		format[at++] = '.';
		format[at++] = '*';
		format[at++] = conversion;
		format[at] = '\0';
		const int shown = precision < 0 && conversion != 'a' && conversion != 'A' ? 6 : precision;
		snprintf(want, sizeof want, format, shown, value);
		const ca::usz length = ca::sys::format_float(ca::span<char>{got, sizeof got}, value, conversion, shown, alternate);
		fuzz::require(got[length] == '\0', "format_float not terminated at its length");
		fuzz::require(strcmp(want, got) == 0, "format_float differs from snprintf");
	}

	auto check_shortest(const double value) noexcept -> void {
		const ca::usz length = ca::sys::format_float_shortest(ca::span<char>{got, sizeof got}, value);
		fuzz::require(got[length] == '\0', "format_float_shortest not terminated at its length");
		if (value - value != 0.0)
			return;
		fuzz::require(strtod(got, nullptr) == value, "format_float_shortest does not read back");
		if (value < SMALLEST_NORMAL && value > -SMALLEST_NORMAL)
			return;
		for (int digits = 15; digits <= 17; ++digits) {
			snprintf(want, sizeof want, "%.*g", digits, value);
			if (strtod(want, nullptr) == value)
				break;
		}
		fuzz::require(strcmp(want, got) == 0 || length < ca::string_length(want), "format_float_shortest differs from the smallest %.Ng");
	}

	auto check_shortest(const float value) noexcept -> void {
		const ca::usz length = ca::sys::format_float_shortest(ca::span<char>{got, sizeof got}, value);
		fuzz::require(got[length] == '\0', "format_float_shortest (f32) not terminated at its length");
		if (value - value != 0.0F)
			return;
		fuzz::require(strtof(got, nullptr) == value, "format_float_shortest (f32) does not read back");
		if (value < SMALLEST_NORMAL_F32 && value > -SMALLEST_NORMAL_F32)
			return;
		for (int digits = 6; digits <= 9; ++digits) {
			snprintf(want, sizeof want, "%.*g", digits, static_cast<double>(value));
			if (strtof(want, nullptr) == value)
				break;
		}
		fuzz::require(strcmp(want, got) == 0 || length < ca::string_length(want), "format_float_shortest (f32) differs from the smallest %.Ng");
	}

} // namespace

extern "C" auto LLVMFuzzerTestOneInput(const ca::u8* const data, const ca::usz size) -> int {

	fuzz::input in{data, size};
	while (in.left() >= 8U) {
		const ca::u64 bits = in.take<ca::u64>();
		const double value = __builtin_bit_cast(double, bits);
		const char conversion = CONVERSIONS[in.below(8U)];
		const int precision = static_cast<int>(in.below(64U)) - 1;
		const bool alternate = (in.take<ca::u8>() & 1U) != 0U && conversion != 'g' && conversion != 'G';
		check_conversion(value, conversion, precision, alternate);
		check_shortest(value);
		check_shortest(__builtin_bit_cast(float, static_cast<ca::u32>(bits >> 16U)));
	}
	return 0;
}
