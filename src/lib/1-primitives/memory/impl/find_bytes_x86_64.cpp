/* memchr and strlen for x86-64: ca::impl::find_byte_variant and string_length_variant, and the
   C symbols memchr and strlen.

   ca::find_byte and ca::string_length are __builtin_memchr and __builtin_strlen: the compiler
   folds literals and calls the C memchr or strlen for the rest, which this file defines as one
   jump to the variant, picked by cpuid on the first call (SSE2, vector V = 16 bytes, or AVX2,
   V = 32).

   Every load is aligned to V: it never crosses a page, so it may cover bytes before the range
   and after its end without faulting (in asm, out of the compiler's sight). The first vector
   is the aligned one holding the start, its mask shifted past the bytes before it.

   | step            | how                                                                |
   |-----------------|--------------------------------------------------------------------|
   | first vector    | equality mask shifted by the misalignment; memchr cuts it at count |
   | strlen          | the next 3 vectors one test each (short strings end there), then a |
   |                 | loop of 4 vectors aligned to 4 V: unsigned minimum, one test       |
   | memchr          | a loop of 4 vectors, equality masks or-ed, one test, while more    |
   |                 | than 4 V remain; then at most 4 V: up to V the aligned vector cut  |
   |                 | at count; up to 2 V it and the vector ending at the end; else the  |
   |                 | next 2 aligned, then the 2 ending at the end (each pair one mask)  |
   | locate in 4     | their masks packed in two words, one bit scan                      |

   memchr loads its first vector unaligned when that cannot cross a page, as glibc: ranges up
   to V bytes take one vector whatever their alignment (mixed sizes 27.6 -> 23.4 cycles). Its
   tail loads the vectors ending at the end unaligned, as glibc, never past it (vector by vector
   before: mixed 23.4 -> 22.3, 64 B 1.24 -> 1.1, 256 B 1.3 -> 1.2; aligned loads, the fourth
   address a cmov or a branch and the masks cut by bzhi, were slower or no faster). The 4-vector
   loops fold their loads into vpcmpeqb or vpminub (asm, one fused uop each) and end in one
   fused test and branch: strlen's is a do-while (a jne out and a jmp back before: 4 KiB 1.33 ->
   1.02), memchr's compares the cursor to the end less 4 V, computed once (4 KiB 1.07 -> 0.92;
   gcc lays it out with the bound test at the top whatever the loop's form: 1.17).

   Per call against glibc 2.39 (AVX2, called without its PLT, perf cycles, i7-10750H, clang):
   strlen 1 - 100 B 1.0 - 1.05, mixed sizes 1.19, 256 B 1.1, 1 - 4 KiB 1.0; memchr mixed sizes
   1.02, 16 - 64 B 1.11 - 1.14, 100 B 1.28, 256 B 1.14, 1 KiB 1.03, 4 KiB 0.91. Known gaps:
   memchr 100 B (15 instructions more than glibc's path), gcc's memchr loop.
*/

#include "1-primitives/memory/impl/find_bytes.hpp"

#include "1-primitives/atomic/memory_order.hpp"
#include "1-primitives/config/architecture.hpp"
#include "1-primitives/config/compiler.hpp"
#include "1-primitives/integers/u32.hpp"
#include "1-primitives/integers/u64.hpp"
#include "1-primitives/integers/u8.hpp"
#include "1-primitives/memory/impl/cpu_features.hpp"
#include "1-primitives/simd/mask_of.hpp"
#include "1-primitives/simd/peek.hpp"
#include "1-primitives/simd/peek_aligned.hpp"
#include "1-primitives/simd/splat.hpp"
#include "1-primitives/simd/vec.hpp"

#if ca_architecture(x86_64) && ca_target_feature(sse2)

#pragma GCC diagnostic ignored "-Wpsabi"

#if ca_compiler(clang)
#	pragma clang attribute push(__attribute__((no_builtin)), apply_to = function)
#else
#	pragma GCC optimize("no-tree-loop-distribute-patterns")
#endif


// -- ACCESS -----------------------------------------------------------------

static constexpr ca::usz PAGE = 4096U;

/* Hides the cursor from the optimizer, which otherwise keeps a base and an index: an indexed
   memory operand of a VEX instruction is unlaminated on Intel, one more uop per load. */
[[gnu::always_inline]] static inline auto unlink(const char*& at) noexcept -> void {
	asm("" : "+r"(at));
}

static auto address(const void* const pointer) noexcept -> ca::usz {
	return reinterpret_cast<ca::usz>(pointer);
}

/* The aligned vector at, loaded. */
template <typename V>
[[gnu::always_inline]] static inline auto load(const char* const at) noexcept -> V {

	V v;
	ca::simd::peek_aligned(at, v);
	return v;
}

/* The vector at, any alignment, all of it in the range. */
template <typename V>
[[gnu::always_inline]] static inline auto loose(const char* const at) noexcept -> V {

	V v;
	ca::simd::peek(at, v);
	return v;
}

/* Bits set where the lanes of v are all ones. */
template <typename V>
[[gnu::always_inline]] static inline auto byte_mask(const V& v) noexcept -> ca::u32 {
	return ca::simd::mask_of(v).bits();
}

/* Lanes of 0xff where the bytes of v equal those of needle. */
template <typename V>
[[gnu::always_inline]] static inline auto equal(const V& v, const V& needle) noexcept -> V {
	return __builtin_bit_cast(V, v == needle);
}

/* Bits set where the bytes of v equal those of needle. */
template <typename V>
[[gnu::always_inline]] static inline auto matches(const V& v, const V& needle) noexcept -> ca::u64 {
	return byte_mask(equal(v, needle));
}

/* equal on the aligned vector at, the load folded into the compare (one fused uop), in asm as
   peek_aligned. */
[[gnu::always_inline]] static inline auto equal_at(const char* const at, const ca::simd::vec<char, 16U>& needle) noexcept -> ca::simd::vec<char, 16U> {

	ca::simd::vec<char, 16U> lanes;
	asm("pcmpeqb %1, %0" : "=x"(lanes) : "m"(*reinterpret_cast<const char(*)[16]>(at)), "0"(needle));
	return lanes;
}

[[gnu::target("avx2")]] static inline auto equal_at(const char* const at, const ca::simd::vec<char, 32U>& needle, ca::simd::vec<char, 32U>& lanes) noexcept -> void {
	asm("vpcmpeqb %1, %2, %0" : "=x"(lanes) : "m"(*reinterpret_cast<const char(*)[32]>(at)), "x"(needle));
}

template <typename V>
[[gnu::always_inline]] static inline auto equal_at(const char* const at, const V& needle) noexcept -> V {

	V lanes;
	equal_at(at, needle, lanes);
	return lanes;
}

/* Lane-wise unsigned minimum of v and the aligned vector at, the load folded, as equal_at. */
[[gnu::always_inline]] static inline auto least_at(const char* const at, const ca::simd::vec<char, 16U>& v) noexcept -> ca::simd::vec<char, 16U> {

	ca::simd::vec<char, 16U> lanes;
	asm("pminub %1, %0" : "=x"(lanes) : "m"(*reinterpret_cast<const char(*)[16]>(at)), "0"(v));
	return lanes;
}

[[gnu::target("avx2")]] static inline auto least_at(const char* const at, const ca::simd::vec<char, 32U>& v, ca::simd::vec<char, 32U>& lanes) noexcept -> void {
	asm("vpminub %1, %2, %0" : "=x"(lanes) : "m"(*reinterpret_cast<const char(*)[32]>(at)), "x"(v));
}

template <typename V>
[[gnu::always_inline]] static inline auto least_at(const char* const at, const V& v) noexcept -> V {

	V lanes;
	least_at(at, v, lanes);
	return lanes;
}

/* value in every byte: a broadcast for 32-byte vectors (mixed sizes 1.06 -> 1.02 against
   glibc), else as 32-bit words, as fill_bytes. */
template <typename V>
[[gnu::always_inline]] static inline auto splat(const int value) noexcept -> V {

	V lanes;
	ca::simd::splat(value, lanes);
	return lanes;
}

/* Lane-wise unsigned minimum: a zero in either lane stays zero. */
template <typename V>
[[gnu::always_inline]] static inline auto least(const V& a, const V& b) noexcept -> V {

	using unsigned_bytes [[gnu::vector_size(sizeof(V))]] = ca::u8;
	const unsigned_bytes x = __builtin_bit_cast(unsigned_bytes, a);
	const unsigned_bytes y = __builtin_bit_cast(unsigned_bytes, b);
	return __builtin_bit_cast(V, x < y ? x : y);
}

/* Offset of the first set bit of 4 masks of N bits, known not all zero: two words, one scan. */
template <ca::usz N>
[[gnu::always_inline]] static inline auto first_of(const ca::u64 m0, const ca::u64 m1, const ca::u64 m2, const ca::u64 m3) noexcept -> ca::usz {

	const ca::u64 low = m0 | m1 << N;
	const ca::u64 high = m2 | m3 << N;
	return low != 0U ? static_cast<ca::usz>(__builtin_ctzll(low)) : 2U * N + static_cast<ca::usz>(__builtin_ctzll(high));
}

static auto low_bits(const ca::usz count) noexcept -> ca::u64 {
	return count >= 64U ? ~ca::u64{0} : (ca::u64{1} << count) - 1U;
}

/* The bits of value below count, all of them from 64: bzhi in the AVX2 variants (flattened,
   they inline it), else low_bits. */
[[gnu::target("bmi2")]] static inline auto zero_high(const ca::u64 value, const ca::usz count) noexcept -> ca::u64 {
	return __builtin_ia32_bzhi_di(value, count);
}

template <typename V>
[[gnu::always_inline]] static inline auto cut(const ca::u64 value, const ca::usz count) noexcept -> ca::u64 {

	if constexpr (sizeof(V) == 32U)
		return zero_high(value, count);
	else
		return value & low_bits(count);
}


// -- STRING LENGTH ----------------------------------------------------------

template <typename V>
[[gnu::always_inline]] static inline auto string_length(const char* const text) noexcept -> ca::usz {

	constexpr ca::usz n = sizeof(V);
	const V zero{};
	const char* at = text - (address(text) & (n - 1U));
	const ca::u64 head = byte_mask(equal_at(at, zero)) >> (address(text) & (n - 1U));
	if (head != 0U)
		return static_cast<ca::usz>(__builtin_ctzll(head));
	for (ca::usz k = 1U; k < 4U; ++k) {
		const ca::u64 found = byte_mask(equal_at(at + k * n, zero));
		if (found != 0U)
			return static_cast<ca::usz>(at + k * n - text) + static_cast<ca::usz>(__builtin_ctzll(found));
	}
	at += 4U * n;
	at -= address(at) & (4U * n - 1U);
	ca::u64 any;
	do {
		const V low = least_at(at + n, load<V>(at));
		const V high = least_at(at + 3U * n, load<V>(at + 2U * n));
		any = matches(least(low, high), zero);
		at += 4U * n;
		unlink(at);
	} while (any == 0U);
	at -= 4U * n;
	const ca::u64 m0 = byte_mask(equal_at(at, zero));
	const ca::u64 m1 = byte_mask(equal_at(at + n, zero));
	const ca::u64 m2 = byte_mask(equal_at(at + 2U * n, zero));
	const ca::u64 m3 = byte_mask(equal_at(at + 3U * n, zero));
	return static_cast<ca::usz>(at - text) + first_of<n>(m0, m1, m2, m3);
}


// -- FIND BYTE --------------------------------------------------------------

template <typename V>
[[gnu::always_inline]] static inline auto find_byte(const void* const data, const int value, const ca::usz count) noexcept -> void* {

	constexpr ca::usz n = sizeof(V);
	if (count == 0U) [[unlikely]]
		return nullptr;
	const char* const start = static_cast<const char*>(data);
	const V needle = splat<V>(value);
	ca::u64 head;
	ca::usz span;
	if ((address(start) & (PAGE - 1U)) <= PAGE - n) [[likely]] {
		head = matches(loose<V>(start), needle);
		span = n;
	} else {
		const ca::usz skip = address(start) & (n - 1U);
		head = byte_mask(equal_at(start - skip, needle)) >> skip;
		span = n - skip;
	}
	if (count <= span) {
		const ca::u64 found = head & low_bits(count);
		return found != 0U ? const_cast<char*>(start + __builtin_ctzll(found)) : nullptr;
	}
	if (head != 0U)
		return const_cast<char*>(start + __builtin_ctzll(head));
	const char* at = start + span;
	at -= address(at) & (n - 1U);
	const char* const end = start + count;
	for (const char* const last = end - 4U * n; at < last; at += 4U * n) {
		const V e0 = equal_at(at, needle);
		const V e1 = equal_at(at + n, needle);
		const V e2 = equal_at(at + 2U * n, needle);
		const V e3 = equal_at(at + 3U * n, needle);
		if (byte_mask((e0 | e1) | (e2 | e3)) != 0U) [[unlikely]] {
			const ca::usz index = first_of<n>(byte_mask(e0), byte_mask(e1), byte_mask(e2), byte_mask(e3));
			return const_cast<char*>(at + index);
		}
		unlink(at);
	}
	const ca::usz left = static_cast<ca::usz>(end - at);
	const ca::u64 first = byte_mask(equal_at(at, needle));
	if (left <= n) {
		const ca::u64 kept = cut<V>(first, left);
		return kept != 0U ? const_cast<char*>(at + __builtin_ctzll(kept)) : nullptr;
	}
	if (left <= 2U * n) {
		const ca::u64 last = matches(loose<V>(end - n), needle);
		if ((first | last) == 0U)
			return nullptr;
		return const_cast<char*>(first != 0U ? at + __builtin_ctzll(first) : end - n + __builtin_ctzll(last));
	}
	const ca::u64 low = first | ca::u64{byte_mask(equal_at(at + n, needle))} << n;
	if (low != 0U)
		return const_cast<char*>(at + __builtin_ctzll(low));
	const ca::u64 high = matches(loose<V>(end - 2U * n), needle) | matches(loose<V>(end - n), needle) << n;
	return high != 0U ? const_cast<char*>(end - 2U * n + __builtin_ctzll(high)) : nullptr;
}


// -- VARIANTS ---------------------------------------------------------------

auto ca::impl::find_byte_sse2(const void* const data, const int value, const ca::usz count) noexcept -> void* {
	return ::find_byte<ca::simd::vec<char, 16U>>(data, value, count);
}

[[gnu::target("avx2,bmi2"), gnu::flatten]] auto ca::impl::find_byte_avx2(const void* const data, const int value, const ca::usz count) noexcept -> void* {
	return ::find_byte<ca::simd::vec<char, 32U>>(data, value, count);
}

auto ca::impl::string_length_sse2(const char* const text) noexcept -> ca::usz {
	return ::string_length<ca::simd::vec<char, 16U>>(text);
}

[[gnu::target("avx2,bmi2"), gnu::flatten]] auto ca::impl::string_length_avx2(const char* const text) noexcept -> ca::usz {
	return ::string_length<ca::simd::vec<char, 32U>>(text);
}


// -- DETECTION --------------------------------------------------------------

static auto has_avx2(void) noexcept -> bool {

	const ca::impl::cpu_features cpu = ca::impl::read_cpu_features();
	return cpu._avx2 && cpu._bmi2;
}

static auto resolve_find(const void* const data, const int value, const ca::usz count) noexcept -> void* {

	const ca::impl::find_byte_function best = has_avx2() ? &ca::impl::find_byte_avx2 : &ca::impl::find_byte_sse2;
	ca::impl::find_byte_variant.store<ca::memory_order::relaxed>(best);
	return best(data, value, count);
}

static auto resolve_length(const char* const text) noexcept -> ca::usz {

	const ca::impl::string_length_function best = has_avx2() ? &ca::impl::string_length_avx2 : &ca::impl::string_length_sse2;
	ca::impl::string_length_variant.store<ca::memory_order::relaxed>(best);
	return best(text);
}


// -- FIND BYTE VARIANT ------------------------------------------------------

constinit ca::atomic<ca::impl::find_byte_function> ca::impl::find_byte_variant{&resolve_find}; // ns-leak: ignore

constinit ca::atomic<ca::impl::string_length_function> ca::impl::string_length_variant{&resolve_length}; // ns-leak: ignore


// -- C SYMBOLS --------------------------------------------------------------

/* What code calling memchr or strlen by name, or the compiler on its behalf (find_byte,
   string_length), links to: one jump to the variant, as glibc's PLT entry. */
extern "C" {

	auto memchr(const void* const data, const int value, const ca::usz count) noexcept -> void* { // ns-leak: ignore
		return ca::impl::find_byte_variant.load<ca::memory_order::relaxed>()(data, value, count);
	}

	auto strlen(const char* const text) noexcept -> ca::usz { // ns-leak: ignore
		return ca::impl::string_length_variant.load<ca::memory_order::relaxed>()(text);
	}

} // extern "C"

#if ca_compiler(clang)
#	pragma clang attribute pop
#endif

#endif
