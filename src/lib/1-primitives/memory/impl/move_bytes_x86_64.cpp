/* memcpy and memmove for x86-64: ca::impl::move_bytes_variant, and the C symbols memcpy and
   memmove.


   1. HOW A COPY IS DONE

   ca::copy_bytes and ca::move_bytes are __builtin_memcpy and __builtin_memmove: the compiler
   expands what it can see (constant or bounded sizes, struct copies) and calls the C memcpy or
   memmove for the rest, which this file defines: up to 64 bytes in place with SSE2 (every
   x86-64 CPU has it, no dispatch), more through one jump to ca::impl::move_bytes_variant.
   Every row of the table below is also public, in ca::move_paths (3-system/memory/
   move_paths.hpp, size-checked, returning results): each size class and strategy, SSE2 and
   AVX2, for callers that know their sizes or their CPU. The variants here are built from the
   same templates (impl/move_paths.hpp). The first call reads the CPU features, picks the SSE2 variant
   (vector V = 16 bytes) or the AVX2 one (V = 32) and stores it there. Every variant is safe on
   overlapping ranges, so memcpy and memmove share it.

   | size (AVX2)     | how                                                                |
   |-----------------|--------------------------------------------------------------------|
   | 0 - 31 B        | two overlapping moves of 1, 2, 4, 8 or 16 bytes, one from each end |
   | 32 - 256 B      | 2, 4 or 8 vectors, half from each end, all loaded before stored    |
   | 257 B - 8 KiB   | loop of 4 vectors with aligned stores, ends saved and stored last  |
   | 8 KiB - L3 / 2  | rep movsb, destination aligned to 64 bytes                         |
   | L3 / 2 and more | non-temporal stores, two pages at a time, with prefetch            |

   - rep movsb and non-temporal stores need ranges that do not overlap; rep movsb needs ERMS.
     Otherwise the loop is used. SSE2 uses rep movsb from 4 KiB.
   - Destination inside the source, above it: the loop runs backward.
   - Destination 0 - 255 bytes ahead of the source modulo 4 KiB (4K aliasing: a load waits on an
     unrelated store with the same low address bits): backward up to 512 B, above that forward
     with every load issued two blocks before the store it could alias.
   - The builtins at the call site matter more than the call itself: an opaque call stops the
     optimizer from inlining callers, folding constant copies away and expanding bounded
     ones. See the last row of 3.


   2. RESULTS AGAINST GLIBC 2.39

   i7-10750H (AVX2, ERMS), idle. Cycles of ca divided by cycles of glibc: below 1.00, ca is
   faster. Three harnesses: a clang build and a gcc build of bench/memcpy.cpp (one binary per
   side), and one binary calling both through a function pointer. Code placement alone moves a
   row by up to 30% between harnesses: a gap is real only when all three columns show it.
   "+1/+3": source and destination 1 and 3 bytes past an aligned address.

   | case                     | clang | gcc  | one binary |
   |--------------------------|-------|------|------------|
   | copy 1 B                 | 0.84  | 0.99 | 0.80       |
   | copy 7 B                 | 1.23  | 1.24 | 1.00       |
   | copy 16 B                | 1.12  | 1.10 | 1.00       |
   | copy 33 B                | 0.80  | 1.05 | 1.00       |
   | copy 64 B                | 0.80  | 0.75 | 1.10       |
   | copy 100 B               | 0.95  | 0.99 | 1.17       |
   | copy 256 B               | 0.98  | 0.98 | 1.03       |
   | copy 300 B               | 1.02  | 1.24 | 1.04       |
   | copy 1 KiB               | 1.05  | 1.09 | 1.05       |
   | copy 4 KiB               | 1.02  | 1.04 | 1.02       |
   | copy 64 KiB              | 1.00  | 0.98 | 0.95       |
   | copy 1 MiB               | 1.00  | 1.00 | 1.00       |
   | copy 100 B +1/+3         | 0.90  | 1.20 | 0.91       |
   | copy 4 KiB +1/+3         | 1.09  | 1.05 | 1.04       |
   | copy 64 KiB +1/+3        | 1.16  | 0.88 | 0.86       |
   | copy 1 MiB +1/+3         | 0.99  | 0.97 | 0.97       |
   | 4 KiB copies over 64 MiB | 0.87  | 0.92 | 0.91       |
   | 1 MiB copies over 64 MiB | 1.00  | 1.00 | 1.09       |
   | 16 MiB copies (streamed) | 1.03  | 1.01 | 1.02       |
   | move 100 B, overlapping  | 0.95  | 0.96 | -          |
   | move 4 KiB, overlapping  | 0.98  | 1.02 | -          |
   | mixed sizes 0 - 2 KiB    | 1.11  | 0.95 | 1.01       |

   Known weak spot: the same 4 KiB copy repeated at a 4K-aliasing distance, 1.04 - 1.09 (the
   last stores of one copy alias the first loads of the next).


   3. HOW IT GOT THERE

   Each step measured with the harnesses above. Before and after: ca / glibc, as in 2.
   "4 KiB, 64 MiB": 4 KiB copies scattered over 64 MiB, so the data comes from DRAM.

   | problem found                  | fix                    | measured on   | before    | after     |
   |--------------------------------|------------------------|---------------|-----------|-----------|
   | entry function, then a jump to | call sites call the    | 33-256 B      | 1.75      | 0.80      |
   | the variant: 4 taken branches  | variant directly       |               |           |           |
   | before any data moves          |                        |               |           |           |
   |--------------------------------|------------------------|---------------|-----------|-----------|
   | size tests ordered unlike      | glibc's order,         | 64 B          | 1.12      | 0.80      |
   | glibc                          | [[likely]]             |               |           |           |
   |--------------------------------|------------------------|---------------|-----------|-----------|
   | gcc used aligned loads on      | packed may_alias       | correctness   | crash     | fixed     |
   | unaligned data: aligned(1) on  | wrapper structs        |               |           |           |
   | a typedef is lost in templates |                        |               |           |           |
   |--------------------------------|------------------------|---------------|-----------|-----------|
   | clang's loop stored through    | unlink() keeps two     | 4 KiB +1/+3   | 1.6       | 1.0       |
   | base + index; indexed stores   | pointers               |               |           |           |
   | cannot use Intel's port 7      |                        |               |           |           |
   |--------------------------------|------------------------|---------------|-----------|-----------|
   | loads stalled on 4K aliasing   | copy backward, as      | 4 KiB +1/+3   | 1.6       | 1.0       |
   |                                | glibc                  |               |           |           |
   |--------------------------------|------------------------|---------------|-----------|-----------|
   | rep movsb slower on an         | align it to 64 first   | 64 KiB-1 MiB  | -         | 1.0       |
   | unaligned destination          |                        |               |           |           |
   |--------------------------------|------------------------|---------------|-----------|-----------|
   | backward copy 12% slower from  | forward loop loading   | 4 KiB, 64 MiB | 1.14      | 0.87-0.92 |
   | DRAM; prefetch does not help   | two blocks ahead       |               |           |           |
   |--------------------------------|------------------------|---------------|-----------|-----------|
   | that loop moved 8 registers    | three register sets    | 4 KiB, cached | 1.20      | 1.0       |
   | per 128 bytes                  | taking turns, unrolled |               |           |           |
   |--------------------------------|------------------------|---------------|-----------|-----------|
   | 5 tests before the loop for    | one size gate first,   | 257-600 B     | 1.03-1.13 | 1.00-1.05 |
   | 257-600 B copies               | as glibc               |               |           |           |
   |--------------------------------|------------------------|---------------|-----------|-----------|
   | (tried) rare paths moved to    | reverted: aliasing     | 257-511 B     | 1.05      | 1.27      |
   | another function               | copies paid a call     |               |           |           |
   |--------------------------------|------------------------|---------------|-----------|-----------|
   | (checked) returning `to`, one  | kept                   | 1-100 B       | 1.0       | 1.0       |
   | mov, as glibc                  |                        |               |           |           |
   |--------------------------------|------------------------|---------------|-----------|-----------|
   | call sites calling the variant | builtins again; calls  | str from 10 B | 13.6      | 1.0       |
   | hid the copy from the          | land in the C memcpy   | literal clang |           |           |
   | optimizer: callers not         | above                  |---------------|-----------|-----------|
   | inlined, constant copies not   |                        | ring 64 B gcc | 3.6       | 0.9       |
   | folded, bounded ones not       |                        |---------------|-----------|-----------|
   | expanded                       |                        | runtime 33-64 | 0.75      | 0.97      |
   |                                |                        | B, clang      |           |           |
   |--------------------------------|------------------------|---------------|-----------|-----------|
   | that extra jump on every small | C memcpy/memmove do    | 1-16 B        | 1.0       | 0.69-0.79 |
   | runtime copy                   | 0-64 B in place, SSE2  |---------------|-----------|-----------|
   |                                |                        | format bench  | 1.0       | 0.83-0.97 |
   |                                |                        |---------------|-----------|-----------|
   |                                | (cost: the size check  | 33-100 B,     | 1.0       | 1.10-1.13 |
   |                                | before the jump)       | clang         |           |           |
   |                                |                        |---------------|-----------|-----------|
   |                                | (tried) in place up to | 33-64 B,      | 1.13      | 1.24      |
   |                                | 32 B only: reverted    | clang         |           |           |
   Before/after of the builtin row: the indirect-call build / the builtin build, each over the
   same tree on glibc. Rows after it: the previous step's build = 1.0. Mixed 0-2 KiB stays
   within noise (0.95-1.07); gcc's 33-100 B rows moved up to 40% between runs of the same
   binary and are not reported.
*/

#include "1-primitives/memory/impl/move_bytes.hpp"

#include "1-primitives/atomic/atomic.hpp"
#include "1-primitives/atomic/memory_order.hpp"
#include "1-primitives/config/architecture.hpp"
#include "1-primitives/config/compiler.hpp"
#include "1-primitives/integers/usz.hpp"
#include "1-primitives/memory/impl/cpu_features.hpp"
#include "1-primitives/memory/impl/loose_bytes.hpp"
#include "1-primitives/memory/impl/move_paths.hpp"
#include "1-primitives/numeric/min.hpp"
#include "1-primitives/simd/vec.hpp"

#if ca_architecture(x86_64) && ca_target_feature(sse2)

#pragma GCC diagnostic ignored "-Wpsabi"

#if ca_compiler(clang)
#	pragma clang attribute push(__attribute__((no_builtin)), apply_to = function)
#else
#	pragma GCC optimize("no-tree-loop-distribute-patterns")
#endif


// -- CA NAMESPACE -----------------------------------------------------------

namespace ca {


	// -- MOVE BYTES X86 64 --------------------------------------------------

	namespace impl {

		/* @DOC Four vectors of N bytes, the unit of the copy loops. */
		template <ca::usz N>
		struct byte_block final {
				using self = ca::impl::byte_block<N>;
				ca::simd::vec<char, N> _v0;
				ca::simd::vec<char, N> _v1;
				ca::simd::vec<char, N> _v2;
				ca::simd::vec<char, N> _v3;
		}; // struct byte_block

	} // namespace impl

} // namespace ca

static constexpr ca::usz PAGE = 4096U;

static constinit ca::atomic<ca::usz> rep_from{static_cast<ca::usz>(-1)}; // file-state: ignore
static constinit ca::atomic<ca::usz> stream_from{static_cast<ca::usz>(-1)}; // file-state: ignore


// -- ACCESS -----------------------------------------------------------------

template <typename T>
[[gnu::always_inline]] static inline auto load(const char* const from) noexcept -> T {
	return reinterpret_cast<const ca::impl::loose_bytes<T>*>(from)->_value;
}

template <typename T>
[[gnu::always_inline]] static inline auto store(char* const to, const T& value) noexcept -> void {
	reinterpret_cast<ca::impl::loose_bytes<T>*>(to)->_value = value;
}

template <typename T>
[[gnu::always_inline]] static inline auto store_aligned(char* const to, const T& value) noexcept -> void {
	reinterpret_cast<ca::impl::aligned_bytes<T>*>(to)->_value = value;
}

template <ca::usz N>
[[gnu::always_inline]] static inline auto stream_block(char* const to, const char* const from) noexcept -> void {

	if constexpr (N == 32U)
		asm volatile(
			"vmovdqu (%1), %%ymm0\n\tvmovdqu 32(%1), %%ymm1\n\tvmovdqu 64(%1), %%ymm2\n\tvmovdqu 96(%1), %%ymm3\n\t"
			"vmovntdq %%ymm0, (%0)\n\tvmovntdq %%ymm1, 32(%0)\n\tvmovntdq %%ymm2, 64(%0)\n\tvmovntdq %%ymm3, 96(%0)"
			: : "r"(to), "r"(from) : "xmm0", "xmm1", "xmm2", "xmm3", "memory");
	else
		asm volatile(
			"movdqu (%1), %%xmm0\n\tmovdqu 16(%1), %%xmm1\n\tmovdqu 32(%1), %%xmm2\n\tmovdqu 48(%1), %%xmm3\n\t"
			"movntdq %%xmm0, (%0)\n\tmovntdq %%xmm1, 16(%0)\n\tmovntdq %%xmm2, 32(%0)\n\tmovntdq %%xmm3, 48(%0)"
			: : "r"(to), "r"(from) : "xmm0", "xmm1", "xmm2", "xmm3", "memory");
}

/* Hides both cursors from the optimizer, so clang keeps two pointers instead of one index:
   indexed stores cannot use the store address port on Intel and contend with the loads. */
template <typename T, typename U>
[[gnu::always_inline]] static inline auto unlink(T*& a, U*& b) noexcept -> void {
	asm("" : "+r"(a), "+r"(b));
}

static auto address(const void* const pointer) noexcept -> ca::usz {
	return reinterpret_cast<ca::usz>(pointer);
}


// -- LARGE ------------------------------------------------------------------

template <ca::usz N>
[[gnu::always_inline]] static inline auto move_loop(char*& to, const char*& from, char* const stop) noexcept -> void {

	using vector = ca::simd::vec<char, N>;
	for (; to < stop; to += 4U * N, from += 4U * N) {
		const vector v0 = load<vector>(from);
		const vector v1 = load<vector>(from + N);
		const vector v2 = load<vector>(from + 2U * N);
		const vector v3 = load<vector>(from + 3U * N);
		store_aligned<vector>(to, v0);
		store_aligned<vector>(to + N, v1);
		store_aligned<vector>(to + 2U * N, v2);
		store_aligned<vector>(to + 3U * N, v3);
		unlink(to, from);
	}
}

template <ca::usz N>
[[gnu::always_inline]] static inline auto load_block(const char* const from) noexcept -> ca::impl::byte_block<N> {

	using vector = ca::simd::vec<char, N>;
	return {load<vector>(from), load<vector>(from + N), load<vector>(from + 2U * N), load<vector>(from + 3U * N)};
}

template <ca::usz N>
[[gnu::always_inline]] static inline auto store_block(char* const to, const ca::impl::byte_block<N>& block) noexcept -> void {

	using vector = ca::simd::vec<char, N>;
	store_aligned<vector>(to, block._v0);
	store_aligned<vector>(to + N, block._v1);
	store_aligned<vector>(to + 2U * N, block._v2);
	store_aligned<vector>(to + 3U * N, block._v3);
}

/* move_loop with every load issued two blocks before the store that could share its address
   modulo 4 KiB, so a destination up to 256 bytes ahead never stalls a load (4K aliasing). Three
   blocks take turns, unrolled, so no vector is copied between registers. */
template <ca::usz N>
[[gnu::always_inline]] static inline auto move_loop_ahead(char*& to, const char*& from, char* const stop) noexcept -> void {

	if (to + 4U * N >= stop) {
		move_loop<N>(to, from, stop);
		return;
	}
	ca::impl::byte_block<N> x = load_block<N>(from);
	ca::impl::byte_block<N> y = load_block<N>(from + 4U * N);
	ca::impl::byte_block<N> z;
	for (;;) {
		if (to + 8U * N >= stop) {
			store_block<N>(to, x);
			store_block<N>(to + 4U * N, y);
			break;
		}
		z = load_block<N>(from + 8U * N);
		store_block<N>(to, x);
		to += 4U * N;
		from += 4U * N;
		unlink(to, from);
		if (to + 8U * N >= stop) {
			store_block<N>(to, y);
			store_block<N>(to + 4U * N, z);
			break;
		}
		x = load_block<N>(from + 8U * N);
		store_block<N>(to, y);
		to += 4U * N;
		from += 4U * N;
		unlink(to, from);
		if (to + 8U * N >= stop) {
			store_block<N>(to, z);
			store_block<N>(to + 4U * N, x);
			break;
		}
		y = load_block<N>(from + 8U * N);
		store_block<N>(to, z);
		to += 4U * N;
		from += 4U * N;
		unlink(to, from);
	}
	to += 8U * N;
	from += 8U * N;
}

template <ca::usz N, bool AHEAD>
[[gnu::always_inline]] static inline auto move_forward(char* to, const char* from, const ca::usz count) noexcept -> void {

	using vector = ca::simd::vec<char, N>;
	char* const start = to;
	char* const end = to + count;
	const vector head = load<vector>(from);
	const vector t0 = load<vector>(from + count - 4U * N);
	const vector t1 = load<vector>(from + count - 3U * N);
	const vector t2 = load<vector>(from + count - 2U * N);
	const vector t3 = load<vector>(from + count - N);
	const ca::usz skip = N - (address(to) & (N - 1U));
	to += skip;
	from += skip;
	if constexpr (AHEAD)
		move_loop_ahead<N>(to, from, end - 4U * N);
	else
		move_loop<N>(to, from, end - 4U * N);
	store<vector>(end - 4U * N, t0);
	store<vector>(end - 3U * N, t1);
	store<vector>(end - 2U * N, t2);
	store<vector>(end - N, t3);
	store<vector>(start, head);
}

template <ca::usz N>
[[gnu::always_inline]] static inline auto move_backward(char* const to, const char* const from, const ca::usz count) noexcept -> void {

	using vector = ca::simd::vec<char, N>;
	const vector tail = load<vector>(from + count - N);
	const vector h0 = load<vector>(from);
	const vector h1 = load<vector>(from + N);
	const vector h2 = load<vector>(from + 2U * N);
	const vector h3 = load<vector>(from + 3U * N);
	char* at = to + count - 1U - ((address(to + count) - 1U) & (N - 1U));
	const char* source = from + (at - to);
	while (at > to + 4U * N) {
		at -= 4U * N;
		source -= 4U * N;
		const vector v0 = load<vector>(source);
		const vector v1 = load<vector>(source + N);
		const vector v2 = load<vector>(source + 2U * N);
		const vector v3 = load<vector>(source + 3U * N);
		store_aligned<vector>(at, v0);
		store_aligned<vector>(at + N, v1);
		store_aligned<vector>(at + 2U * N, v2);
		store_aligned<vector>(at + 3U * N, v3);
		unlink(at, source);
	}
	store<vector>(to, h0);
	store<vector>(to + N, h1);
	store<vector>(to + 2U * N, h2);
	store<vector>(to + 3U * N, h3);
	store<vector>(to + count - N, tail);
}

template <ca::usz N>
[[gnu::always_inline]] static inline auto move_repeat(char* to, const char* from, ca::usz count) noexcept -> void {

	using vector = ca::simd::vec<char, N>;
	char* const start = to;
	const vector h0 = load<vector>(from);
	const vector h1 = load<vector>(from + N);
	const vector h2 = load<vector>(from + 2U * N);
	const vector h3 = load<vector>(from + 3U * N);
	const ca::usz skip = 64U - (address(to) & 63U);
	to += skip;
	from += skip;
	count -= skip;
	asm volatile("rep movsb" : "+D"(to), "+S"(from), "+c"(count) : : "memory");
	store<vector>(start, h0);
	store<vector>(start + N, h1);
	store<vector>(start + 2U * N, h2);
	store<vector>(start + 3U * N, h3);
}

template <ca::usz N>
[[gnu::always_inline]] static inline auto move_stream(char* to, const char* from, const ca::usz count) noexcept -> void {

	using vector = ca::simd::vec<char, N>;
	char* const start = to;
	char* const end = to + count;
	const vector h0 = load<vector>(from);
	const vector h1 = load<vector>(from + N);
	const vector h2 = load<vector>(from + 2U * N);
	const vector h3 = load<vector>(from + 3U * N);
	const vector t0 = load<vector>(from + count - 4U * N);
	const vector t1 = load<vector>(from + count - 3U * N);
	const vector t2 = load<vector>(from + count - 2U * N);
	const vector t3 = load<vector>(from + count - N);
	const ca::usz skip = 64U - (address(to) & 63U);
	to += skip;
	from += skip;
	for (ca::usz pairs = static_cast<ca::usz>(end - to) / (2U * PAGE); pairs != 0U; --pairs) {
		for (ca::usz i = 0U; i < PAGE; i += 4U * N) {
			for (ca::usz line = 0U; line < 4U * N; line += 64U) {
				__builtin_prefetch(from + i + 4U * N + line, 0, 3);
				__builtin_prefetch(from + PAGE + i + 4U * N + line, 0, 3);
			}
			stream_block<N>(to + i, from + i);
			stream_block<N>(to + PAGE + i, from + PAGE + i);
		}
		to += 2U * PAGE;
		from += 2U * PAGE;
	}
	asm volatile("sfence" ::: "memory");
	move_loop<N>(to, from, end - 4U * N);
	store<vector>(end - 4U * N, t0);
	store<vector>(end - 3U * N, t1);
	store<vector>(end - 2U * N, t2);
	store<vector>(end - N, t3);
	store<vector>(start, h0);
	store<vector>(start + N, h1);
	store<vector>(start + 2U * N, h2);
	store<vector>(start + 3U * N, h3);
}

template <ca::usz N>
[[gnu::always_inline]] static inline auto move_large(char* const to, const char* const from, const ca::usz count) noexcept -> void {

	const ca::usz ahead = address(to) - address(from);
	if (count >= rep_from.load<ca::memory_order::relaxed>()) [[unlikely]] {
		if (ahead >= count && address(from) - address(to) >= count) {
			if (count >= stream_from.load<ca::memory_order::relaxed>())
				move_stream<N>(to, from, count);
			else
				move_repeat<N>(to, from, count);
			return;
		}
	}
	if (ahead < count) [[unlikely]] {
		move_backward<N>(to, from, count);
		return;
	}
	if ((ahead & (PAGE - 256U)) == 0U) [[unlikely]] {
		if (count <= 16U * N)
			move_backward<N>(to, from, count);
		else
			move_forward<N, true>(to, from, count);
		return;
	}
	move_forward<N, false>(to, from, count);
}


// -- VARIANTS ---------------------------------------------------------------

template <ca::usz N>
[[gnu::always_inline]] static inline auto move_any(void* const target, const void* const source, const ca::usz count) noexcept -> void {

	using vector = ca::simd::vec<char, N>;
	char* const to = static_cast<char*>(target);
	const char* const from = static_cast<const char*>(source);
	if (count < N) [[unlikely]] {
		ca::impl::move_small(to, from, count);
		return;
	}
	if (count <= 2U * N) [[likely]] {
		ca::impl::move_two<vector>(to, from, count);
		return;
	}
	if (count <= 4U * N) {
		ca::impl::move_four<vector>(to, from, count);
		return;
	}
	if (count <= 8U * N) {
		ca::impl::move_eight<vector>(to, from, count);
		return;
	}
	move_large<N>(to, from, count);
}

static auto move_sse2(void* const to, const void* const from, const ca::usz count) noexcept -> void* {

	move_any<16U>(to, from, count);
	return to;
}

[[gnu::target("avx2")]] static auto move_avx2(void* const to, const void* const from, const ca::usz count) noexcept -> void* {

	move_any<32U>(to, from, count);
	return to;
}


// -- DETECTION --------------------------------------------------------------

/* Sets the rep movsb and streaming thresholds from the CPU; whether it has AVX2. */
static auto detect(void) noexcept -> bool {

	const ca::impl::cpu_features cpu = ca::impl::read_cpu_features();
	const ca::usz cache = cpu._last_level_cache;
	stream_from.store<ca::memory_order::relaxed>(cache / 2U);
	rep_from.store<ca::memory_order::relaxed>(cpu._erms ? ca::min<ca::usz>(cpu._avx2 ? 8192U : 4096U, cache / 2U) : cache / 2U);
	return cpu._avx2;
}

static auto resolve(void* const to, const void* const from, const ca::usz count) noexcept -> void* {

	const ca::impl::move_bytes_function best = detect() ? &move_avx2 : &move_sse2;
	ca::impl::move_bytes_variant.store<ca::memory_order::relaxed>(best);
	return best(to, from, count);
}

/* The thresholds, for paths called before any copy went through the variant. */
[[gnu::always_inline]] static inline auto ensure_detected(void) noexcept -> void {
	if (rep_from.load<ca::memory_order::relaxed>() == static_cast<ca::usz>(-1)) [[unlikely]]
		static_cast<void>(detect());
}


// -- MOVE PATHS -------------------------------------------------------------

auto ca::impl::paths::forward(void* const to, const void* const from, const ca::usz count) noexcept -> void {
	move_forward<16U, false>(static_cast<char*>(to), static_cast<const char*>(from), count);
}

auto ca::impl::paths::backward(void* const to, const void* const from, const ca::usz count) noexcept -> void {
	move_backward<16U>(static_cast<char*>(to), static_cast<const char*>(from), count);
}

auto ca::impl::paths::repeat(void* const to, const void* const from, const ca::usz count) noexcept -> void {
	move_repeat<16U>(static_cast<char*>(to), static_cast<const char*>(from), count);
}

auto ca::impl::paths::stream(void* const to, const void* const from, const ca::usz count) noexcept -> void {
	move_stream<16U>(static_cast<char*>(to), static_cast<const char*>(from), count);
}

auto ca::impl::paths::large(void* const to, const void* const from, const ca::usz count) noexcept -> void {

	ensure_detected();
	move_large<16U>(static_cast<char*>(to), static_cast<const char*>(from), count);
}

auto ca::impl::paths::any(void* const to, const void* const from, const ca::usz count) noexcept -> void {

	ensure_detected();
	move_any<16U>(to, from, count);
}

[[gnu::target("avx2")]] auto ca::impl::paths::avx2::forward(void* const to, const void* const from, const ca::usz count) noexcept -> void {
	move_forward<32U, false>(static_cast<char*>(to), static_cast<const char*>(from), count);
}

[[gnu::target("avx2")]] auto ca::impl::paths::avx2::backward(void* const to, const void* const from, const ca::usz count) noexcept -> void {
	move_backward<32U>(static_cast<char*>(to), static_cast<const char*>(from), count);
}

[[gnu::target("avx2")]] auto ca::impl::paths::avx2::repeat(void* const to, const void* const from, const ca::usz count) noexcept -> void {
	move_repeat<32U>(static_cast<char*>(to), static_cast<const char*>(from), count);
}

[[gnu::target("avx2")]] auto ca::impl::paths::avx2::stream(void* const to, const void* const from, const ca::usz count) noexcept -> void {
	move_stream<32U>(static_cast<char*>(to), static_cast<const char*>(from), count);
}

[[gnu::target("avx2")]] auto ca::impl::paths::avx2::large(void* const to, const void* const from, const ca::usz count) noexcept -> void {

	ensure_detected();
	move_large<32U>(static_cast<char*>(to), static_cast<const char*>(from), count);
}

[[gnu::target("avx2")]] auto ca::impl::paths::avx2::any(void* const to, const void* const from, const ca::usz count) noexcept -> void {

	ensure_detected();
	move_any<32U>(to, from, count);
}


// -- MOVE BYTES VARIANT -----------------------------------------------------

constinit ca::atomic<ca::impl::move_bytes_function> ca::impl::move_bytes_variant{&resolve}; // ns-leak: ignore


// -- C SYMBOLS --------------------------------------------------------------

/* What code calling memcpy or memmove by name, or the compiler on its behalf (copy_bytes,
   struct copies, recognized loops), links to: up to 64 bytes in place, more through one jump to
   the variant, as glibc's PLT entry. */
extern "C" {

	auto memcpy(void* const to, const void* const from, const ca::usz count) noexcept -> void* { // ns-leak: ignore

		if (count <= 64U) [[likely]] {
			ca::impl::move_few(static_cast<char*>(to), static_cast<const char*>(from), count);
			return to;
		}
		return ca::impl::move_bytes_variant.load<ca::memory_order::relaxed>()(to, from, count);
	}

	auto memmove(void* const to, const void* const from, const ca::usz count) noexcept -> void* { // ns-leak: ignore

		if (count <= 64U) [[likely]] {
			ca::impl::move_few(static_cast<char*>(to), static_cast<const char*>(from), count);
			return to;
		}
		return ca::impl::move_bytes_variant.load<ca::memory_order::relaxed>()(to, from, count);
	}

} // extern "C"

#if ca_compiler(clang)
#	pragma clang attribute pop
#endif

#endif
