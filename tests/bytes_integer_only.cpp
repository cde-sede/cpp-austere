// expect: exit 0
// shared: bytes ../src/lib/1-primitives/memory/impl/move_bytes_integer_x86_64.cpp -mgeneral-regs-only src/lib/1-primitives/memory/impl/fill_bytes_integer_x86_64.cpp src/lib/1-primitives/memory/impl/compare_bytes_integer_x86_64.cpp src/lib/1-primitives/memory/impl/find_bytes_integer_x86_64.cpp src/lib/1-primitives/memory/impl/cpu_features_x86_64.cpp
// The byte primitives of an integer-only build (-mgeneral-regs-only), loaded from a library
// built so: memcpy, memmove, memset, memcmp, bcmp, memchr and strlen against byte loops.
#include "1-primitives/integers/usz.hpp"
#include "6-resources/dl/library.hpp"

using move_function = void*(void*, const void*, ca::usz) noexcept;
using fill_function = void*(void*, int, ca::usz) noexcept;
using compare_function = int(const void*, const void*, ca::usz) noexcept;
using find_function = void*(const void*, int, ca::usz) noexcept;
using length_function = ca::usz(const char*) noexcept;

static constexpr ca::usz LARGE = 70000U;

alignas(4096) static char work[LARGE + 8192U];
alignas(4096) static char other[LARGE + 8192U];
alignas(4096) static char expected[LARGE + 8192U];

static const ca::usz offsets[] = {0U, 1U, 3U, 7U, 8U, 15U};
static const ca::usz deltas[] = {1U, 7U, 8U, 31U, 32U, 33U, 100U, 4096U};
static const ca::usz large_sizes[] = {1023U, 1024U, 2047U, 2048U, 4095U, 4096U, 4097U, 65537U, LARGE};

static auto pattern(const ca::usz i, const unsigned seed) -> char {
	return static_cast<char>((i * 131U + seed) ^ (i >> 8U));
}

static auto fill(char* const to, const ca::usz count, const unsigned seed) -> void {
	for (ca::usz i = 0; i < count; ++i)
		to[i] = pattern(i, seed);
}

static auto same(const char* const a, const char* const b, const ca::usz count) -> bool {
	for (ca::usz i = 0; i < count; ++i)
		if (a[i] != b[i])
			return false;
	return true;
}

static auto sign(const int value) -> int {
	return value < 0 ? -1 : value > 0 ? 1 : 0;
}

static auto move_case(move_function* const move, const ca::usz count, const ca::usz from, const ca::usz to) -> bool {
	const ca::usz span = count + (from > to ? from : to) + 64U;
	fill(work, span, 5U);
	fill(expected, span, 5U);
	for (ca::usz i = 0; i < count; ++i)
		expected[to + i] = pattern(from + i, 5U);
	return move(work + to, work + from, count) == work + to && same(work, expected, span);
}

static auto copy_case(move_function* const copy, const ca::usz count, const ca::usz from, const ca::usz to) -> bool {
	const ca::usz span = count + to + 64U;
	fill(other, count + from, 9U);
	fill(work, span, 3U);
	fill(expected, span, 3U);
	for (ca::usz i = 0; i < count; ++i)
		expected[to + i] = other[from + i];
	return copy(work + to, other + from, count) == work + to && same(work, expected, span);
}

static auto fill_case(fill_function* const set, const ca::usz count, const ca::usz to, const int value) -> bool {
	const ca::usz span = count + to + 64U;
	fill(work, span, 7U);
	fill(expected, span, 7U);
	for (ca::usz i = 0; i < count; ++i)
		expected[to + i] = static_cast<char>(value);
	return set(work + to, value, count) == work + to && same(work, expected, span);
}

/* a and b equal over count, then b changed at at (if below count): memcmp's sign and bcmp's
   zero must follow the byte there, compared as unsigned. */
static auto compare_case(compare_function* const compare, compare_function* const differ, const ca::usz count, const ca::usz offset, const ca::usz at, const char changed) -> bool {
	fill(work + offset, count, 11U);
	fill(other + offset, count, 11U);
	int want = 0;
	if (at < count) {
		const unsigned char before = static_cast<unsigned char>(other[offset + at]);
		other[offset + at] = changed;
		const unsigned char after = static_cast<unsigned char>(changed);
		want = before < after ? -1 : before > after ? 1 : 0;
	}
	const bool ordered = sign(compare(work + offset, other + offset, count)) == want;
	const bool differs = (differ(work + offset, other + offset, count) != 0) == (want != 0);
	return ordered && differs;
}

static auto find_case(find_function* const find, const ca::usz count, const ca::usz offset, const ca::usz at) -> bool {
	char* const start = work + 4096U + offset;
	for (ca::usz i = 0; i < count + 64U; ++i)
		start[i] = 'a';
	start[-1] = 'x';
	start[count] = 'x';
	if (at < count)
		start[at] = 'x';
	void* const found = find(start, 'x', count);
	return at < count ? found == start + at : found == nullptr;
}

static auto length_case(length_function* const length, const ca::usz count, const ca::usz offset) -> bool {
	char* const start = work + 4096U + offset;
	start[-1] = '\0';
	for (ca::usz i = 0; i < count; ++i)
		start[i] = static_cast<char>(1 + i % 255U);
	start[count] = '\0';
	start[count + 1U] = 'a';
	return length(start) == count;
}

auto main(void) -> int {
	auto library = ca::dl::library::make(CA_TEST_LIBRARIES "/libbytes.so");
	if (!library)
		return 1;
	const auto copy = library->find_as<move_function>("memcpy");
	const auto move = library->find_as<move_function>("memmove");
	const auto set = library->find_as<fill_function>("memset");
	const auto compare = library->find_as<compare_function>("memcmp");
	const auto differ = library->find_as<compare_function>("bcmp");
	const auto find = library->find_as<find_function>("memchr");
	const auto length = library->find_as<length_function>("strlen");
	if (!copy || !move || !set || !compare || !differ || !find || !length)
		return 2;
	for (ca::usz count = 0; count <= 600U; ++count)
		for (const ca::usz from : offsets)
			for (const ca::usz to : offsets)
				if (!copy_case(*copy, count, from, to))
					return 3;
	for (ca::usz count = 0; count <= 600U; count += count < 130U ? 1U : 13U)
		for (const ca::usz delta : deltas)
			for (const ca::usz base : offsets) {
				if (!move_case(*move, count, base + delta, base))
					return 4;
				if (!move_case(*move, count, base, base + delta))
					return 5;
			}
	for (const ca::usz count : large_sizes)
		for (const ca::usz shift : offsets) {
			if (!copy_case(*copy, count, shift, 8U - shift))
				return 6;
			if (!move_case(*move, count, shift + 100U, shift))
				return 7;
			if (!move_case(*move, count, shift, shift + 100U))
				return 8;
		}
	for (ca::usz count = 0; count <= 600U; ++count)
		for (const ca::usz to : offsets)
			if (!fill_case(*set, count, to, 0x5a) || !fill_case(*set, count, to, -1) || !fill_case(*set, count, to, 0x180))
				return 9;
	for (const ca::usz count : large_sizes)
		for (const ca::usz to : offsets)
			if (!fill_case(*set, count, to, 0x33))
				return 10;
	for (ca::usz count = 0; count <= 100U; ++count)
		for (const ca::usz offset : offsets)
			for (ca::usz at = 0; at <= count; ++at)
				if (!compare_case(*compare, *differ, count, offset, at, '\x01') || !compare_case(*compare, *differ, count, offset, at, '\xf0'))
					return 11;
	for (const ca::usz count : large_sizes)
		for (const ca::usz at : {ca::usz{0}, count / 2U, count - 1U, count})
			if (!compare_case(*compare, *differ, count, 3U, at, '\x80') || !compare_case(*compare, *differ, count, 3U, at, '\x00'))
				return 12;
	for (ca::usz count = 0; count <= 100U; ++count)
		for (const ca::usz offset : offsets) {
			for (ca::usz at = 0; at <= count; ++at)
				if (!find_case(*find, count, offset, at))
					return 13;
			if (!length_case(*length, count, offset))
				return 14;
		}
	for (const ca::usz count : large_sizes)
		if (!find_case(*find, count, 5U, count - 1U) || !find_case(*find, count, 5U, count) || !length_case(*length, count, 5U))
			return 15;
	return 0;
}
