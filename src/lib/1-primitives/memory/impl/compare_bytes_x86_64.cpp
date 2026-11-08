/* memcmp and bcmp for x86-64: ca::impl::compare_bytes_variant and differ_bytes_variant, and the
   C symbols memcmp and bcmp.

   1. HOW A COMPARISON IS DONE

   ca::compare_bytes is __builtin_memcmp: the compiler folds constant comparisons, turns
   memcmp(...) == 0 into bcmp (clang), and calls the C memcmp or bcmp for the rest, which this
   file defines as one jump to the variant. The first call picks it from cpuid (SSE2, vector
   V = 16 bytes, or AVX2 with BMI2, V = 32).

   | size (AVX2)     | how                                                                |
   |-----------------|--------------------------------------------------------------------|
   | 0 - 1 B         | apart: a null pointer is valid with 0                              |
   | 2 - 31 B        | one 32-byte compare masked to count: the load may run past the     |
   |                 | range, never past a page (asm, out of the compiler's sight). Near  |
   |                 | a page end: big-endian integers of 2, 4 or 8 bytes from each end,  |
   |                 | or two 16-byte vectors                                             |
   | 32 - 64 B       | one vector from each end, masks and-ed, one test                   |
   | 65 - 128 B      | two vectors from each end, one test                                |
   | 129 - 256 B     | four vectors from each end, one test per four                      |
   | 257 B and more  | loop of 4 vectors, one test each, the last 4 vectors overlapping   |

   - The first vector is tested alone first, as glibc: a difference in the first bytes returns
     at once. In a block of 4, the first difference is a bit scan over their masks packed in two
     words. memcmp returns the difference of the first unequal bytes, bcmp only 1.
   - Bytes before the first difference are equal, so overlapping vectors never change a result.


   2. HOW IT GOT THERE

   Measured per call (perf cycles, i7-10750H) against glibc 2.39 called without its PLT.

   | problem found                         | fix                                  | before | after |
   |---------------------------------------|--------------------------------------|--------|-------|
   | benchmark hoisted the pure call out   | pointer through an empty asm         | -      | -     |
   | of its loop                           |                                      |        |       |
   | gcc kept a loop over the 4 vectors of | spelled out                          | 2.0x   | 1.2x  |
   | a block (1 - 4 KiB)                   |                                      |        |       |
   | indexed VEX loads unlaminated (clang  | unlink(): two pointers               | 1.25x  | 1.0x  |
   | loop, 1 - 4 KiB)                      |                                      |        |       |
   | clang tested masks with vpxor+vptest, | mask through an empty asm            | 1.3x   | 1.08x |
   | then compared again (100 B)           |                                      |        |       |
   | ladder mispredicting on mixed sizes   | masked compare under the page check  | -      | -     |
   | 33 - 64 B: 4 tests and taken branches | one vector from each end, masks      | 11.2   | 7.1   |
   | per class, then 4 vectors where 2 do  | and-ed, one test                     |        |       |
   | in place up to 64 B with SSE2         | one jump to the variant              | 20.2   | 19.3  |
   |                                       |                                      | (mix)  |       |
   | (tried) in place below 16 B only      | reverted: mix 21.6                   |        |       |
   | shl %cl for the mask (3 uops)         | AVX2 variant needs BMI2: shlx        | -      | -     |
   | (tried) seal the classes, as memset   | reverted: no gain, mix worse         |        |       |
   | all vectors and-ed before any test:   | first vector alone first; locate a   | 2.4x   | 1.35x |
   | an early difference paid for the      | block of 4 by one bit scan           |        |       |
   | whole range (100 B, bench)            |                                      |        |       |
   | (tried) variants aligned to 64 bytes  | reverted: within noise               |        |       |
   | above 64 B, a size test then a jump   | first vector tested before any size  | 10.1   | 8.2   |
   | into a merged exit before the first   | dispatch                             |        |       |
   | vector's verdict (difference at 0)    |                                      |        |       |
   | below 32 B: two loads, mask built by  | b's load folded into vpcmpeqb (asm), | 11.2   | 9.2   |
   | shift and not, exit merged into a     | mask + 1 as the difference bits,     |        |       |
   | jump (difference at 3)                | the exit sealed                      |        |       |

   bench/memcmp.cpp, ca / glibc cycles (glibc through its PLT), clang and gcc:
   33 B - 1 MiB 0.93 - 1.2, mixed sizes 1.01 - 1.09, a difference in the first bytes 1.12.
   Known gaps: 1 - 16 B 1.1 - 1.25 (glibc's PLT jump versus the jump through the variant and
   one more instruction), at a random place of 256 B 1.27 - 1.34. gcc rows move up to 30%
   between runs of the same binary. Fixed sizes below 64 B are whole cycles set by code
   placement: built with -mbranches-within-32B-boundaries (the JCC erratum of this CPU) 33 and
   64 B go 9 -> 8 cycles, 1 B 10 -> 12, mixed sizes unchanged; the mix is the figure to trust.
*/

#include "1-primitives/memory/impl/compare_bytes.hpp"

#include "1-primitives/atomic/memory_order.hpp"
#include "1-primitives/config/architecture.hpp"
#include "1-primitives/config/compiler.hpp"
#include "1-primitives/integers/u16.hpp"
#include "1-primitives/integers/u32.hpp"
#include "1-primitives/integers/u64.hpp"
#include "1-primitives/integers/u8.hpp"
#include "1-primitives/memory/impl/cpu_features.hpp"
#include "1-primitives/memory/impl/loose_bytes.hpp"
#include "1-primitives/simd/mask_of.hpp"
#include "1-primitives/simd/peek.hpp"
#include "1-primitives/simd/vec.hpp"

#if ca_architecture(x86_64) && ca_target_feature(sse2)

#pragma GCC diagnostic ignored "-Wpsabi"

#if ca_compiler(clang)
#	pragma clang attribute push(__attribute__((no_builtin)), apply_to = function)
#else
#	pragma GCC optimize("no-tree-loop-distribute-patterns")
#endif

static constexpr ca::usz PAGE = 4096U;


// -- ACCESS -----------------------------------------------------------------

template <typename T>
[[gnu::always_inline]] static inline auto get(const char* const from) noexcept -> T {
	return ca::impl::loose<T>(from)._value;
}

template <typename V>
[[gnu::always_inline]] static inline auto mask(const V& v) noexcept -> ca::u32 {
	return ca::simd::mask_of(v).bits();
}

/* Lanes of 0xff where a and b hold the same byte. */
template <typename V>
[[gnu::always_inline]] static inline auto same(const char* const a, const char* const b) noexcept -> V {
	return __builtin_bit_cast(V, get<V>(a) == get<V>(b));
}

template <typename V>
static constexpr ca::u32 ALL = sizeof(V) == 16U ? 0xffffU : 0xffffffffU;

[[gnu::always_inline]] static inline auto difference(const char* const a, const char* const b) noexcept -> int {
	return static_cast<int>(static_cast<ca::u8>(*a)) - static_cast<int>(static_cast<ca::u8>(*b));
}

[[gnu::always_inline]] static inline auto address(const void* const pointer) noexcept -> ca::usz {
	return reinterpret_cast<ca::usz>(pointer);
}

[[gnu::always_inline]] static inline auto order(const ca::u64 a, const ca::u64 b) noexcept -> int {
	return static_cast<int>(a > b) - static_cast<int>(a < b);
}


// -- COMPARE ----------------------------------------------------------------

/* 0 - 15 bytes. */
[[gnu::always_inline]] static inline auto compare_ladder(const char* const a, const char* const b, const ca::usz count) noexcept -> int {

	if (count >= 8U) [[unlikely]] {
		const ca::u64 x = __builtin_bswap64(get<ca::u64>(a));
		const ca::u64 y = __builtin_bswap64(get<ca::u64>(b));
		if (x != y)
			return order(x, y);
		return order(__builtin_bswap64(get<ca::u64>(a + count - 8U)), __builtin_bswap64(get<ca::u64>(b + count - 8U)));
	}
	if (count >= 4U) [[unlikely]] {
		const ca::u64 x = static_cast<ca::u64>(__builtin_bswap32(get<ca::u32>(a))) << 32U | __builtin_bswap32(get<ca::u32>(a + count - 4U));
		const ca::u64 y = static_cast<ca::u64>(__builtin_bswap32(get<ca::u32>(b))) << 32U | __builtin_bswap32(get<ca::u32>(b + count - 4U));
		return order(x, y);
	}
	if (count >= 2U) [[unlikely]] {
		const ca::u32 x = static_cast<ca::u32>(__builtin_bswap16(get<ca::u16>(a))) << 8U | static_cast<ca::u8>(a[count - 1U]);
		const ca::u32 y = static_cast<ca::u32>(__builtin_bswap16(get<ca::u16>(b))) << 8U | static_cast<ca::u8>(b[count - 1U]);
		return static_cast<int>(x) - static_cast<int>(y);
	}
	return count != 0U ? difference(a, b) : 0;
}

/* Whether the 4 vectors at a and b differ: one test. */
template <typename V>
[[gnu::always_inline]] static inline auto block_differs(const char* const a, const char* const b) noexcept -> bool {

	constexpr ca::usz n = sizeof(V);
	return mask((same<V>(a, b) & same<V>(a + n, b + n)) & (same<V>(a + 2U * n, b + 2U * n) & same<V>(a + 3U * n, b + 3U * n))) != ALL<V>;
}

/* Bits set where the bytes of the vectors at a and b differ. */
template <typename V>
[[gnu::always_inline]] static inline auto differ_bits(const char* const a, const char* const b) noexcept -> ca::u32 {
	return mask(same<V>(a, b)) ^ ALL<V>;
}

/* What a difference at a + at, b + at returns: the byte difference (memcmp) or 1 (bcmp). */
template <bool LOCATE>
[[gnu::always_inline]] static inline auto found(const char* const a, const char* const b, const ca::usz at) noexcept -> int {
	return LOCATE ? difference(a + at, b + at) : 1;
}

/* Offset of the first difference in the 4 vectors at a and b, known to differ: their bits in two
   words, one branch. */
template <typename V>
[[gnu::always_inline]] static inline auto locate_block(const char* const a, const char* const b) noexcept -> ca::usz {

	constexpr ca::usz n = sizeof(V);
	const ca::u64 low = differ_bits<V>(a, b) | static_cast<ca::u64>(differ_bits<V>(a + n, b + n)) << n;
	const ca::u64 high = differ_bits<V>(a + 2U * n, b + 2U * n) | static_cast<ca::u64>(differ_bits<V>(a + 3U * n, b + 3U * n)) << n;
	return low != 0U ? static_cast<ca::usz>(__builtin_ctzll(low)) : 2U * n + static_cast<ca::usz>(__builtin_ctzll(high));
}

/* count in [sizeof(V), 2 sizeof(V)]: the first vector, then the last, one test each, as glibc:
   a difference in the first bytes returns at once. */
template <typename V, bool LOCATE>
[[gnu::always_inline]] static inline auto compare_pair(const char* const a, const char* const b, const ca::usz count) noexcept -> int {

	const ca::u32 head = differ_bits<V>(a, b);
	if (head != 0U) [[unlikely]]
		return found<LOCATE>(a, b, static_cast<ca::usz>(__builtin_ctz(head)));
	const ca::usz last = count - sizeof(V);
	const ca::u32 tail = differ_bits<V>(a + last, b + last);
	if (tail != 0U) [[unlikely]]
		return found<LOCATE>(a, b, last + static_cast<ca::usz>(__builtin_ctz(tail)));
	return 0;
}

/* count in [2 sizeof(V), 4 sizeof(V)], the first vector found equal: the second and the last
   two in one test. */
template <typename V, bool LOCATE>
[[gnu::always_inline]] static inline auto compare_four(const char* const a, const char* const b, const ca::usz count) noexcept -> int {

	constexpr ca::usz n = sizeof(V);
	const ca::usz third = count - 2U * n;
	const ca::usz last = count - n;
	if (mask((same<V>(a + n, b + n) & same<V>(a + third, b + third)) & same<V>(a + last, b + last)) == ALL<V>) [[likely]]
		return 0;
	const ca::u32 second = differ_bits<V>(a + n, b + n);
	if (second != 0U)
		return found<LOCATE>(a, b, n + static_cast<ca::usz>(__builtin_ctz(second)));
	const ca::u32 next = differ_bits<V>(a + third, b + third);
	if (next != 0U)
		return found<LOCATE>(a, b, third + static_cast<ca::usz>(__builtin_ctz(next)));
	return found<LOCATE>(a, b, last + static_cast<ca::usz>(__builtin_ctz(differ_bits<V>(a + last, b + last))));
}

/* count in [4 sizeof(V), 8 sizeof(V)]: four vectors from each end, one test each half. */
template <typename V, bool LOCATE>
[[gnu::always_inline]] static inline auto compare_eight(const char* const a, const char* const b, const ca::usz count) noexcept -> int {

	const ca::usz tail = count - 4U * sizeof(V);
	if (block_differs<V>(a, b)) [[unlikely]]
		return found<LOCATE>(a, b, locate_block<V>(a, b));
	if (block_differs<V>(a + tail, b + tail)) [[unlikely]]
		return found<LOCATE>(a, b, tail + locate_block<V>(a + tail, b + tail));
	return 0;
}

/* Hides both cursors from the optimizer, so clang keeps two pointers instead of one index:
   an indexed memory operand of a VEX instruction is unlaminated on Intel, one more uop per
   compare. */
[[gnu::always_inline]] static inline auto unlink(const char*& a, const char*& b) noexcept -> void {
	asm("" : "+r"(a), "+r"(b));
}

template <typename V, bool LOCATE>
[[gnu::always_inline]] static inline auto compare_loop(const char* a, const char* b, const ca::usz count) noexcept -> int {

	constexpr ca::usz block = 4U * sizeof(V);
	const char* const last = a + count - block;
	const char* const other = b + count - block;
	for (; a < last; a += block, b += block) {
		if (block_differs<V>(a, b)) [[unlikely]]
			return found<LOCATE>(a, b, locate_block<V>(a, b));
		unlink(a, b);
	}
	if (block_differs<V>(last, other))
		return found<LOCATE>(last, other, locate_block<V>(last, other));
	return 0;
}


// -- DIFFER -----------------------------------------------------------------

template <typename T>
[[gnu::always_inline]] static inline auto ends_differ(const char* const a, const char* const b, const ca::usz count) noexcept -> int {
	return static_cast<int>(((get<T>(a) ^ get<T>(b)) | (get<T>(a + count - sizeof(T)) ^ get<T>(b + count - sizeof(T)))) != 0U);
}

/* 0 - 15 bytes. */
[[gnu::always_inline]] static inline auto differ_ladder(const char* const a, const char* const b, const ca::usz count) noexcept -> int {

	if (count >= 8U) [[unlikely]]
		return ends_differ<ca::u64>(a, b, count);
	if (count >= 4U) [[unlikely]]
		return ends_differ<ca::u32>(a, b, count);
	if (count >= 2U) [[unlikely]]
		return ends_differ<ca::u16>(a, b, count);
	return count != 0U ? static_cast<int>(*a != *b) : 0;
}


// -- FEW --------------------------------------------------------------------

/* Ends a path with an asm of its own, so the compiler does not merge its exit with the others
   into a jump: on the small-size mismatch, 11.2 -> 10.1 cycles (sealing the vector paths' exits
   too moved code and cost 1 cycle at 40 - 100 B: left out). */
template <int K>
[[gnu::always_inline]] static inline auto seal(void) noexcept -> void {
	asm("# %c0" : : "i"(K));
}

/* The bits of value below count (< 32): bzhi with BMI2 (the AVX2 variants, flattened, inline
   it; clang otherwise builds the mask with mov, shlx and not: 9.2 -> 8.4 cycles at 7 B), else a
   mask. */
[[gnu::target("bmi2")]] static inline auto zero_high(const ca::u32 value, const ca::u32 count) noexcept -> ca::u32 {
	return __builtin_ia32_bzhi_si(value, count);
}

template <typename V>
[[gnu::always_inline]] static inline auto low_bits(const ca::u32 value, const ca::u32 count) noexcept -> ca::u32 {

	if constexpr (sizeof(V) == 32U)
		return zero_high(value, count);
	else
		return value & ((1U << count) - 1U);
}

/* Equality lanes of the vectors at a and b, which may run past the range (see
   ca::simd::peek), loaded in asm. AVX2 folds b's load into vpcmpeqb; legacy SSE pcmpeqb faults
   on an unaligned memory operand, so the 16-byte one loads both. */
[[gnu::always_inline]] static inline auto same_peeked(const char* const a, const char* const b, ca::simd::vec<char, 16U>& lanes) noexcept -> void {

	ca::simd::vec<char, 16U> x;
	ca::simd::vec<char, 16U> y;
	ca::simd::peek(a, x);
	ca::simd::peek(b, y);
	lanes = __builtin_bit_cast(ca::simd::vec<char, 16U>, x == y);
}

[[gnu::target("avx2")]] static inline auto same_peeked(const char* const a, const char* const b, ca::simd::vec<char, 32U>& lanes) noexcept -> void {

	ca::simd::vec<char, 32U> x;
	ca::simd::peek(a, x);
	asm("vpcmpeqb %1, %2, %0" : "=x"(lanes) : "m"(*reinterpret_cast<const char(*)[32]>(b)), "x"(x));
}

/* 0 - (sizeof(V) - 1) bytes. From 2, one vector compare masked to count when neither pointer
   is within sizeof(V) bytes of a page end (a page boundary is never crossed, so the extra bytes
   cannot fault): one branch for every size, where the ladder mispredicts on mixed sizes. The
   mask + 1 has its lowest bit at the first difference (and none when all lanes are equal), so
   one and with the count's bits tests and locates it (bzhi with BMI2). Near a page end, the
   ladder or two 16-byte vectors. 0 and 1 apart: a null pointer is valid with 0. */
template <typename V, bool LOCATE>
[[gnu::always_inline]] static inline auto compare_small(const char* const a, const char* const b, const ca::usz count) noexcept -> int {

	if (count <= 1U) [[unlikely]]
		return count != 0U ? (LOCATE ? difference(a, b) : static_cast<int>(*a != *b)) : 0;
	if (((address(a) | address(b)) & (PAGE - 1U)) > PAGE - sizeof(V)) [[unlikely]] {
		if (count >= 16U)
			return compare_pair<ca::simd::vec<char, 16U>, LOCATE>(a, b, count);
		return LOCATE ? compare_ladder(a, b, count) : differ_ladder(a, b, count);
	}
	V lanes;
	same_peeked(a, b, lanes);
	const ca::u32 differ = low_bits<V>(mask(lanes) + 1U, static_cast<ca::u32>(count));
	if constexpr (LOCATE) {
		if (differ == 0U)
			return 0;
		const unsigned at = static_cast<unsigned>(__builtin_ctz(differ));
		seal<1>();
		return difference(a + at, b + at);
	} else {
		return static_cast<int>(differ != 0U);
	}
}


// -- VARIANTS ---------------------------------------------------------------

template <typename V, bool LOCATE>
[[gnu::always_inline]] static inline auto compare_any(const void* const left, const void* const right, const ca::usz count) noexcept -> int {

	constexpr ca::usz n = sizeof(V);
	const char* const a = static_cast<const char*>(left);
	const char* const b = static_cast<const char*>(right);
	if (count < n) [[unlikely]]
		return compare_small<V, LOCATE>(a, b, count);
	const ca::u32 head = differ_bits<V>(a, b);
	if (head != 0U) [[unlikely]]
		return found<LOCATE>(a, b, static_cast<ca::usz>(__builtin_ctz(head)));
	if (count <= 2U * n) [[likely]] {
		const ca::usz last = count - n;
		const ca::u32 tail = differ_bits<V>(a + last, b + last);
		return tail != 0U ? found<LOCATE>(a, b, last + static_cast<ca::usz>(__builtin_ctz(tail))) : 0;
	}
	if (count <= 4U * n)
		return compare_four<V, LOCATE>(a, b, count);
	if (count <= 8U * n)
		return compare_eight<V, LOCATE>(a, b, count);
	return compare_loop<V, LOCATE>(a, b, count);
}

auto ca::impl::compare_bytes_sse2(const void* const a, const void* const b, const ca::usz count) noexcept -> int {
	return compare_any<ca::simd::vec<char, 16U>, true>(a, b, count);
}

[[gnu::target("avx2,bmi2"), gnu::flatten]] auto ca::impl::compare_bytes_avx2(const void* const a, const void* const b, const ca::usz count) noexcept -> int {
	return compare_any<ca::simd::vec<char, 32U>, true>(a, b, count);
}

auto ca::impl::differ_bytes_sse2(const void* const a, const void* const b, const ca::usz count) noexcept -> int {
	return compare_any<ca::simd::vec<char, 16U>, false>(a, b, count);
}

[[gnu::target("avx2,bmi2"), gnu::flatten]] auto ca::impl::differ_bytes_avx2(const void* const a, const void* const b, const ca::usz count) noexcept -> int {
	return compare_any<ca::simd::vec<char, 32U>, false>(a, b, count);
}


// -- DETECTION --------------------------------------------------------------

static auto has_avx2(void) noexcept -> bool {

	const ca::impl::cpu_features cpu = ca::impl::read_cpu_features();
	return cpu._avx2 && cpu._bmi2;
}

static auto resolve_compare(const void* const a, const void* const b, const ca::usz count) noexcept -> int {

	const ca::impl::compare_bytes_function best = has_avx2() ? &ca::impl::compare_bytes_avx2 : &ca::impl::compare_bytes_sse2;
	ca::impl::compare_bytes_variant.store<ca::memory_order::relaxed>(best);
	return best(a, b, count);
}

static auto resolve_differ(const void* const a, const void* const b, const ca::usz count) noexcept -> int {

	const ca::impl::compare_bytes_function best = has_avx2() ? &ca::impl::differ_bytes_avx2 : &ca::impl::differ_bytes_sse2;
	ca::impl::differ_bytes_variant.store<ca::memory_order::relaxed>(best);
	return best(a, b, count);
}


// -- COMPARE BYTES VARIANT --------------------------------------------------

constinit ca::atomic<ca::impl::compare_bytes_function> ca::impl::compare_bytes_variant{&resolve_compare}; // ns-leak: ignore

constinit ca::atomic<ca::impl::compare_bytes_function> ca::impl::differ_bytes_variant{&resolve_differ}; // ns-leak: ignore


// -- C SYMBOLS --------------------------------------------------------------

/* What code calling memcmp or bcmp by name, or the compiler on its behalf (compare_bytes,
   equal_units, defaulted ==), links to: one jump to the variant, as glibc's PLT entry. */
extern "C" {

	auto memcmp(const void* const a, const void* const b, const ca::usz count) noexcept -> int { // ns-leak: ignore
		return ca::impl::compare_bytes_variant.load<ca::memory_order::relaxed>()(a, b, count);
	}

	auto bcmp(const void* const a, const void* const b, const ca::usz count) noexcept -> int { // ns-leak: ignore
		return ca::impl::differ_bytes_variant.load<ca::memory_order::relaxed>()(a, b, count);
	}

} // extern "C"

#if ca_compiler(clang)
#	pragma clang attribute pop
#endif

#endif
