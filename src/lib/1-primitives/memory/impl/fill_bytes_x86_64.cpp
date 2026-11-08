/* memset for x86-64: ca::impl::fill_bytes_variant, and the C symbol memset.

   1. HOW A FILL IS DONE

   ca::fill_bytes is __builtin_memset: the compiler expands constant and bounded sizes and calls
   the C memset for the rest, which this file defines as one jump to
   ca::impl::fill_bytes_variant. The first call picks it from cpuid (SSE2, vector V = 16 bytes,
   or AVX2, V = 32), as ca::impl::move_bytes_variant.

   | size (AVX2)     | how                                                                |
   |-----------------|--------------------------------------------------------------------|
   | 0 - 15 B        | two overlapping stores of 1, 2, 4 or 8 bytes, one at each end      |
   | 16 - 31 B       | two overlapping 16-byte stores                                     |
   | 32 - 256 B      | 2, 4 or 8 vectors, half at each end                                |
   | 257 B - 32 KiB  | loop of 4 aligned vectors, the head and the last 4 vectors apart   |
   | 32 KiB and more | rep stosb (ERMS only; the loop otherwise)                          |

   Measured per call (perf cycles, i7-10750H) against glibc 2.39 called without its PLT:

   | problem found                         | fix                                  | before | after |
   |---------------------------------------|--------------------------------------|--------|-------|
   | gcc built a 32-byte splat with 32     | broadcast a 32-bit word pattern      | 5.5x   | 1.0x  |
   | vpinsrb (100 B)                       |                                      |        |       |
   | identical last stores of the size     | an asm of its own ends each class    | 11.2   | 7.1   |
   | classes merged into jumps (100 B)     | (seal)                               |        |       |
   | C memset in place up to 64 B with     | one jump to the variant: 2 ymm       | 22.6   | 19.4  |
   | SSE2: 4 stores where AVX2 needs 2,    | stores, the jump costs 1 cycle       | (mix)  |       |
   | and its ladder on mixed sizes         |                                      |        |       |
   | rep stosb from 2 KiB (glibc's)        | from 32 KiB: the loop is faster up   | 90     | 67    |
   |                                       | to 28 KiB here (2 KiB)               |        |       |
   | the word pattern's imul before the    | vmovd, vpbroadcastb (shufflevector): | 13.4   | 10.1  |
   | first store (gcc 100 B; clang mix     | clang mix 17.9 -> 17.1               |        |       |
   | 1.01, its fixed sizes 1 cycle better) |                                      |        |       |

   bench/memset.cpp, ca / glibc cycles (glibc through its PLT), clang and gcc: 16 B - 1 MiB and
   streaming 0.55 - 1.14, mixed sizes 0.99 - 1.09. Known gap: 1 - 7 B up to 1.22.
*/

#include "1-primitives/memory/impl/fill_bytes.hpp"

#include "1-primitives/atomic/memory_order.hpp"
#include "1-primitives/config/architecture.hpp"
#include "1-primitives/config/compiler.hpp"
#include "1-primitives/integers/u16.hpp"
#include "1-primitives/integers/u32.hpp"
#include "1-primitives/integers/u64.hpp"
#include "1-primitives/integers/u8.hpp"
#include "1-primitives/memory/impl/cpu_features.hpp"
#include "1-primitives/memory/impl/loose_bytes.hpp"
#include "1-primitives/simd/splat.hpp"
#include "1-primitives/simd/vec.hpp"

#if ca_architecture(x86_64) && ca_target_feature(sse2)

#pragma GCC diagnostic ignored "-Wpsabi"

#if ca_compiler(clang)
#	pragma clang attribute push(__attribute__((no_builtin)), apply_to = function)
#else
#	pragma GCC optimize("no-tree-loop-distribute-patterns")
#endif

static constinit ca::atomic<ca::usz> rep_from{static_cast<ca::usz>(-1)}; // file-state: ignore


// -- ACCESS -----------------------------------------------------------------

template <typename T>
[[gnu::always_inline]] static inline auto put(char* const to, const T& value) noexcept -> void {
	ca::impl::loose<T>(to)._value = value;
}

template <typename T>
[[gnu::always_inline]] static inline auto put_aligned(char* const to, const T& value) noexcept -> void {
	reinterpret_cast<ca::impl::aligned_bytes<T>*>(to)->_value = value;
}

/* value in every byte: a broadcast for 32-byte vectors, else as 32-bit words (gcc builds a
   byte broadcast of V{} + value one vpinsrb at a time). */
template <typename V>
[[gnu::always_inline]] static inline auto splat(const int value) noexcept -> V {

	V lanes;
	ca::simd::splat(value, lanes);
	return lanes;
}

static auto address(const void* const pointer) noexcept -> ca::usz {
	return reinterpret_cast<ca::usz>(pointer);
}

/* Ends a size class with an asm of its own, so the compiler does not merge the identical last
   stores of the classes into a chain of jumps: two taken branches more from 65 bytes. */
template <int K>
[[gnu::always_inline]] static inline auto seal(void) noexcept -> void {
	asm("# %c0" : : "i"(K));
}


// -- SMALL ------------------------------------------------------------------

/* Two pieces of T, one at each end: count in [sizeof(T), 2 sizeof(T)]. */
template <typename T>
[[gnu::always_inline]] static inline auto fill_two(char* const to, const T& value, const ca::usz count) noexcept -> void {

	put<T>(to, value);
	put<T>(to + count - sizeof(T), value);
}

template <typename V>
[[gnu::always_inline]] static inline auto fill_four(char* const to, const V& value, const ca::usz count) noexcept -> void {

	constexpr ca::usz n = sizeof(V);
	put<V>(to, value);
	put<V>(to + n, value);
	put<V>(to + count - 2U * n, value);
	put<V>(to + count - n, value);
}

template <typename V>
[[gnu::always_inline]] static inline auto fill_eight(char* const to, const V& value, const ca::usz count) noexcept -> void {

	constexpr ca::usz n = sizeof(V);
	put<V>(to, value);
	put<V>(to + n, value);
	put<V>(to + 2U * n, value);
	put<V>(to + 3U * n, value);
	put<V>(to + count - 4U * n, value);
	put<V>(to + count - 3U * n, value);
	put<V>(to + count - 2U * n, value);
	put<V>(to + count - n, value);
}

/* 0 - 15 bytes. */
[[gnu::always_inline]] static inline auto fill_ladder(char* const to, const int value, const ca::usz count) noexcept -> void {

	const ca::u64 pattern = static_cast<ca::u8>(value) * 0x0101010101010101U;
	if (count >= 8U) [[unlikely]] {
		fill_two<ca::u64>(to, pattern, count);
		seal<-8>();
	} else if (count >= 4U) [[unlikely]] {
		fill_two<ca::u32>(to, static_cast<ca::u32>(pattern), count);
		seal<-4>();
	} else if (count >= 2U) [[unlikely]] {
		fill_two<ca::u16>(to, static_cast<ca::u16>(pattern), count);
		seal<-2>();
	} else if (count != 0U) {
		*to = static_cast<char>(value);
	}
}


// -- LARGE ------------------------------------------------------------------

template <typename V>
[[gnu::always_inline]] static inline auto fill_loop(char* const to, const V& value, const ca::usz count) noexcept -> void {

	constexpr ca::usz n = sizeof(V);
	char* const end = to + count;
	char* const stop = end - 4U * n;
	put<V>(to, value);
	for (char* at = to + n - (address(to) & (n - 1U)); at < stop; at += 4U * n) {
		put_aligned<V>(at, value);
		put_aligned<V>(at + n, value);
		put_aligned<V>(at + 2U * n, value);
		put_aligned<V>(at + 3U * n, value);
	}
	put<V>(stop, value);
	put<V>(stop + n, value);
	put<V>(stop + 2U * n, value);
	put<V>(end - n, value);
}

[[gnu::always_inline]] static inline auto fill_repeat(char* to, const int value, ca::usz count) noexcept -> void {
	asm volatile("rep stosb" : "+D"(to), "+c"(count) : "a"(value) : "memory");
}


// -- VARIANTS ---------------------------------------------------------------

template <typename V>
[[gnu::always_inline]] static inline auto fill_any(void* const target, const int value, const ca::usz count) noexcept -> void {

	constexpr ca::usz n = sizeof(V);
	char* const to = static_cast<char*>(target);
	if (count < n) [[unlikely]] {
		if (n > 16U && count >= 16U)
			fill_two(to, splat<ca::simd::vec<char, 16U>>(value), count);
		else
			fill_ladder(to, value, count);
		return;
	}
	const V v = splat<V>(value);
	if (count <= 2U * n) [[likely]] {
		fill_two(to, v, count);
		seal<2>();
		return;
	}
	if (count <= 4U * n) {
		fill_four(to, v, count);
		seal<4>();
		return;
	}
	if (count <= 8U * n) {
		fill_eight(to, v, count);
		seal<8>();
		return;
	}
	if (count >= rep_from.load<ca::memory_order::relaxed>()) [[unlikely]] {
		fill_repeat(to, value, count);
		return;
	}
	fill_loop(to, v, count);
}

auto ca::impl::fill_bytes_sse2(void* const to, const int value, const ca::usz count) noexcept -> void* {

	fill_any<ca::simd::vec<char, 16U>>(to, value, count);
	return to;
}

[[gnu::target("avx2")]] auto ca::impl::fill_bytes_avx2(void* const to, const int value, const ca::usz count) noexcept -> void* {

	fill_any<ca::simd::vec<char, 32U>>(to, value, count);
	return to;
}


// -- DETECTION --------------------------------------------------------------

static auto resolve(void* const to, const int value, const ca::usz count) noexcept -> void* {

	const ca::impl::cpu_features cpu = ca::impl::read_cpu_features();
	rep_from.store<ca::memory_order::relaxed>(cpu._erms ? 32768U : static_cast<ca::usz>(-1));
	const ca::impl::fill_bytes_function best = cpu._avx2 ? &ca::impl::fill_bytes_avx2 : &ca::impl::fill_bytes_sse2;
	ca::impl::fill_bytes_variant.store<ca::memory_order::relaxed>(best);
	return best(to, value, count);
}


// -- FILL BYTES VARIANT -----------------------------------------------------

constinit ca::atomic<ca::impl::fill_bytes_function> ca::impl::fill_bytes_variant{&resolve}; // ns-leak: ignore


// -- C SYMBOLS --------------------------------------------------------------

/* What code calling memset by name, or the compiler on its behalf (fill_bytes, zeroed structs,
   recognized loops), links to: one jump to the variant, as glibc's PLT entry. */
extern "C" {

	auto memset(void* const to, const int value, const ca::usz count) noexcept -> void* { // ns-leak: ignore
		return ca::impl::fill_bytes_variant.load<ca::memory_order::relaxed>()(to, value, count);
	}

} // extern "C"

#if ca_compiler(clang)
#	pragma clang attribute pop
#endif

#endif
