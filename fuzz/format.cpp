// The run-time half of ca::format, rendering, on arbitrary values: integers against snprintf
// (%lld %llx %llo %llb, sign, zero padding, width), floats against snprintf, char text of any
// bytes passed through as it is (padding counting an invalid subpart as one code point), UTF-16
// of any units transcoded to valid UTF-8 at least as wide as asked, {:?} valid and quoted. The specs themselves are parsed and checked at compile time (consteval),
// where a fuzzer cannot reach: they are fixed here.
#include "1-primitives/integers/i64.hpp"
#include "1-primitives/integers/u16.hpp"
#include "1-primitives/integers/u64.hpp"
#include "2-vocabulary/sv/u16.hpp"
#include "2-vocabulary/unicode/count_code_points.hpp"
#include "2-vocabulary/unicode/is_valid.hpp"
#include "5-collections/format/format.hpp"
#include "5-collections/string/str.hpp"
#include "fuzz/fuzz.hpp"

extern "C" auto snprintf(char* out, ca::usz size, const char* format, ...) noexcept -> int;

namespace {

	char want[512];

	template <typename R>
	auto equal(R&& made, const char* const what) noexcept -> void {
		if (!made)
			return;
		fuzz::require(made->view() == ca::sv::str{want}, what);
		fuzz::require(static_cast<bool>(made->cleanup()), "str cleanup");
	}

	template <typename R>
	auto text(R&& made, const ca::usz width, const bool quoted, const char* const what) noexcept -> void {
		if (!made)
			return;
		const ca::sv::str out = made->view();
		fuzz::require(ca::unicode::is_valid(out), what);
		fuzz::require(ca::unicode::count_code_points(out) >= width, "text narrower than its width");
		if (quoted)
			fuzz::require(out.size() >= 2U && out[0] == '"' && out[out.size() - 1U] == '"', "{:?} not quoted");
		fuzz::require(static_cast<bool>(made->cleanup()), "str cleanup");
	}

	auto integers(fuzz::input& in) noexcept -> void {
		const ca::i64 s = in.take<ca::i64>();
		const ca::u64 u = in.take<ca::u64>();
		const long long ls = s;
		const unsigned long long lu = u;
		snprintf(want, sizeof want, "%lld", ls);
		equal(ca::format("{}", s), "{} of an i64");
		snprintf(want, sizeof want, "%+012lld", ls);
		equal(ca::format("{:+012}", s), "{:+012} of an i64");
		snprintf(want, sizeof want, "%25lld", ls);
		equal(ca::format("{:25}", s), "{:25} of an i64");
		snprintf(want, sizeof want, "%-25llu|", lu);
		equal(ca::format("{:<25}|", u), "{:<25} of a u64");
		snprintf(want, sizeof want, "%llx", lu);
		equal(ca::format("{:x}", u), "{:x} of a u64");
		snprintf(want, sizeof want, "%llX", lu);
		equal(ca::format("{:X}", u), "{:X} of a u64");
		snprintf(want, sizeof want, "%llo", lu);
		equal(ca::format("{:o}", u), "{:o} of a u64");
		snprintf(want, sizeof want, "%llb", lu);
		equal(ca::format("{:b}", u), "{:b} of a u64");
		if (u != 0U) {
			snprintf(want, sizeof want, "%#llx", lu);
			equal(ca::format("{:#x}", u), "{:#x} of a u64");
		}
	}

	auto floats(fuzz::input& in) noexcept -> void {
		const double value = __builtin_bit_cast(double, in.take<ca::u64>());
		snprintf(want, sizeof want, "%.3e", value);
		equal(ca::format("{:.3e}", value), "{:.3e} of an f64");
		snprintf(want, sizeof want, "%30.6f", value);
		if (value < 1e20 && value > -1e20)
			equal(ca::format("{:30.6f}", value), "{:30.6f} of an f64");
		snprintf(want, sizeof want, "%.17g", value);
		equal(ca::format("{:.17g}", value), "{:.17g} of an f64");
		text(ca::format("{:^40}", value), 40U, false, "{:^40} of an f64 not UTF-8");
	}

	/* char text goes out as it is, valid or not: padded with fill, cut to precision code points
	   (an invalid subpart counting as one). */
	template <typename R>
	auto padded(R&& made, const ca::sv::str bytes, const ca::usz width, const char fill, const char* const what) noexcept -> void {
		if (!made)
			return;
		const ca::sv::str out = made->view();
		const ca::usz points = ca::unicode::count_code_points(bytes);
		const ca::usz pad = width > points ? width - points : 0U;
		fuzz::require(out.size() == bytes.size() + pad, what);
		bool framed = false;
		for (ca::usz before = 0U; before <= pad && !framed; ++before) {
			framed = out.without_prefix(before).starts_with(bytes);
			for (ca::usz i = 0U; i < out.size() && framed; ++i)
				framed = (i >= before && i < before + bytes.size()) || out[i] == fill;
		}
		fuzz::require(framed, what);
		fuzz::require(static_cast<bool>(made->cleanup()), "str cleanup");
	}

	auto texts(fuzz::input& in) noexcept -> void {
		const ca::sv::str bytes = in.text(128U);
		auto plain = ca::format("{}", bytes);
		if (plain) {
			fuzz::require(plain->view() == bytes, "{} of bytes changed them");
			fuzz::require(static_cast<bool>(plain->cleanup()), "str cleanup");
		}
		text(ca::format("{:?}", bytes), 0U, true, "{:?} of bytes not UTF-8");
		padded(ca::format("{:^30}", bytes), bytes, 30U, ' ', "{:^30} of bytes");
		padded(ca::format("{:*>30}", bytes), bytes, 30U, '*', "{:*>30} of bytes");
		static char16_t units[64];
		ca::usz count = 0U;
		while (count < 64U && in.left() >= 2U)
			units[count++] = in.take<char16_t>();
		const ca::sv::u16 wide{units, count};
		text(ca::format("{}", wide), 0U, false, "{} of UTF-16 not UTF-8");
		text(ca::format("{:?}", wide), 0U, true, "{:?} of UTF-16 not UTF-8");
		text(ca::format("{:>30}", wide), 30U, false, "{:>30} of UTF-16 not UTF-8");
		const char32_t point = static_cast<char32_t>(in.take<ca::u32>() % 0x120000U);
		text(ca::format("{}", point), 0U, false, "{} of a char32_t not UTF-8");
		text(ca::format("{:?}", point), 0U, false, "{:?} of a char32_t not UTF-8");
	}

} // namespace

extern "C" auto LLVMFuzzerTestOneInput(const ca::u8* const data, const ca::usz size) -> int {

	fuzz::input in{data, size};
	switch (in.take<ca::u8>() % 3U) {
	case 0: integers(in); break;
	case 1: floats(in); break;
	default: texts(in); break;
	}
	return 0;
}
