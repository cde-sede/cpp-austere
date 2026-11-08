/* memcpy and memmove for an integer-only x86-64 build (-mgeneral-regs-only, -mno-sse): no
   vector register is touched, so they are safe where the vector state is not saved.

   | size            | how                                                                |
   |-----------------|--------------------------------------------------------------------|
   | 0 - 16 B        | two overlapping moves of 1, 2, 4 or 8 bytes, one from each end     |
   | 17 - 64 B       | 4 or 8 words, half from each end, all loaded before stored         |
   | 65 B and more   | loop of 4 words, the 32 bytes of the far end saved and stored last |
   | from rep_from   | rep movsb, ranges that do not overlap                              |

   Destination inside the source, above it: the loop runs backward. rep_from is 512 B with
   ERMS, else 4 KiB (not measured), from cpuid on the first call through
   ca::impl::move_bytes_variant. i7-10750H (ERMS), cycles of rep movsb divided by the loop's,
   clang and gcc, aligned and +1/+3: 1.5 - 2.1 at 128 B, 0.96 - 1.10 at 256 B, 0.87 - 1.03 at
   384 B, 0.74 - 0.86 at 512 B, 0.26 - 0.50 from 4 KiB.
*/

#include "1-primitives/memory/impl/move_bytes.hpp"

#include "1-primitives/atomic/atomic.hpp"
#include "1-primitives/atomic/memory_order.hpp"
#include "1-primitives/config/architecture.hpp"
#include "1-primitives/config/compiler.hpp"
#include "1-primitives/integers/u64.hpp"
#include "1-primitives/integers/usz.hpp"
#include "1-primitives/memory/impl/cpu_features.hpp"
#include "1-primitives/memory/impl/loose_bytes.hpp"
#include "1-primitives/memory/impl/move_paths.hpp"

#if ca_architecture(x86_64) && !ca_target_feature(sse2)

#if ca_compiler(clang)
#	pragma clang attribute push(__attribute__((no_builtin)), apply_to = function)
#else
#	pragma GCC optimize("no-tree-loop-distribute-patterns")
#endif

static constinit ca::atomic<ca::usz> rep_from{static_cast<ca::usz>(-1)}; // file-state: ignore


// -- ACCESS -----------------------------------------------------------------

[[gnu::always_inline]] static inline auto load(const char* const from) noexcept -> ca::u64 {
	return ca::impl::loose<ca::u64>(from)._value;
}

[[gnu::always_inline]] static inline auto store(char* const to, const ca::u64 value) noexcept -> void {
	ca::impl::loose<ca::u64>(to)._value = value;
}

static auto address(const void* const pointer) noexcept -> ca::usz {
	return reinterpret_cast<ca::usz>(pointer);
}


// -- LARGE ------------------------------------------------------------------

/* to at or below from, or no overlap. */
[[gnu::always_inline]] static inline auto move_forward(char* to, const char* from, const ca::usz count) noexcept -> void {

	char* const stop = to + count - 32U;
	const ca::u64 t0 = load(from + count - 32U);
	const ca::u64 t1 = load(from + count - 24U);
	const ca::u64 t2 = load(from + count - 16U);
	const ca::u64 t3 = load(from + count - 8U);
	for (; to < stop; to += 32U, from += 32U) {
		const ca::u64 w0 = load(from);
		const ca::u64 w1 = load(from + 8U);
		const ca::u64 w2 = load(from + 16U);
		const ca::u64 w3 = load(from + 24U);
		store(to, w0);
		store(to + 8U, w1);
		store(to + 16U, w2);
		store(to + 24U, w3);
	}
	store(stop, t0);
	store(stop + 8U, t1);
	store(stop + 16U, t2);
	store(stop + 24U, t3);
}

/* to at or above from, or no overlap. */
[[gnu::always_inline]] static inline auto move_backward(char* const to, const char* const from, const ca::usz count) noexcept -> void {

	const ca::u64 h0 = load(from);
	const ca::u64 h1 = load(from + 8U);
	const ca::u64 h2 = load(from + 16U);
	const ca::u64 h3 = load(from + 24U);
	char* at = to + count;
	const char* source = from + count;
	while (at > to + 32U) {
		at -= 32U;
		source -= 32U;
		const ca::u64 w0 = load(source);
		const ca::u64 w1 = load(source + 8U);
		const ca::u64 w2 = load(source + 16U);
		const ca::u64 w3 = load(source + 24U);
		store(at, w0);
		store(at + 8U, w1);
		store(at + 16U, w2);
		store(at + 24U, w3);
	}
	store(to, h0);
	store(to + 8U, h1);
	store(to + 16U, h2);
	store(to + 24U, h3);
}

[[gnu::always_inline]] static inline auto move_repeat(char* to, const char* from, ca::usz count) noexcept -> void {
	asm volatile("rep movsb" : "+D"(to), "+S"(from), "+c"(count) : : "memory");
}

[[gnu::always_inline]] static inline auto move_large(char* const to, const char* const from, const ca::usz count) noexcept -> void {

	const ca::usz ahead = address(to) - address(from);
	if (ahead < count) [[unlikely]]
		move_backward(to, from, count);
	else if (count >= rep_from.load<ca::memory_order::relaxed>() && address(from) - address(to) >= count) [[unlikely]]
		move_repeat(to, from, count);
	else
		move_forward(to, from, count);
}


// -- VARIANT ----------------------------------------------------------------

/* 0 - 64 bytes: what the C memcpy and memmove do in place. */
[[gnu::always_inline]] static inline auto move_few(char* const to, const char* const from, const ca::usz count) noexcept -> void {

	if (count <= 16U) [[likely]]
		ca::impl::move_ladder(to, from, count);
	else if (count <= 32U)
		ca::impl::move_four<ca::u64>(to, from, count);
	else
		ca::impl::move_eight<ca::u64>(to, from, count);
}

static auto move_integer(void* const to, const void* const from, const ca::usz count) noexcept -> void* {

	if (count <= 64U)
		move_few(static_cast<char*>(to), static_cast<const char*>(from), count);
	else
		move_large(static_cast<char*>(to), static_cast<const char*>(from), count);
	return to;
}

static auto resolve(void* const to, const void* const from, const ca::usz count) noexcept -> void* {

	rep_from.store<ca::memory_order::relaxed>(ca::impl::read_cpu_features()._erms ? 512U : 4096U);
	ca::impl::move_bytes_variant.store<ca::memory_order::relaxed>(&move_integer);
	return move_integer(to, from, count);
}


// -- MOVE BYTES VARIANT -----------------------------------------------------

constinit ca::atomic<ca::impl::move_bytes_function> ca::impl::move_bytes_variant{&resolve}; // ns-leak: ignore


// -- C SYMBOLS --------------------------------------------------------------

/* Up to 64 bytes in place, more through one jump to the variant. Every path loads what it moves
   before storing over it, so memcpy and memmove are the same. */
extern "C" {

	auto memcpy(void* const to, const void* const from, const ca::usz count) noexcept -> void* { // ns-leak: ignore

		if (count <= 64U) [[likely]] {
			move_few(static_cast<char*>(to), static_cast<const char*>(from), count);
			return to;
		}
		return ca::impl::move_bytes_variant.load<ca::memory_order::relaxed>()(to, from, count);
	}

	auto memmove(void* const to, const void* const from, const ca::usz count) noexcept -> void* { // ns-leak: ignore

		if (count <= 64U) [[likely]] {
			move_few(static_cast<char*>(to), static_cast<const char*>(from), count);
			return to;
		}
		return ca::impl::move_bytes_variant.load<ca::memory_order::relaxed>()(to, from, count);
	}

} // extern "C"

#if ca_compiler(clang)
#	pragma clang attribute pop
#endif

#endif
