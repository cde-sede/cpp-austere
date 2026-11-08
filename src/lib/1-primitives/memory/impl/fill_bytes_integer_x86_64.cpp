/* memset for an integer-only x86-64 build (-mgeneral-regs-only, -mno-sse): the byte in every
   lane of a 64-bit word, no vector register touched.

   | size            | how                                                          |
   |-----------------|--------------------------------------------------------------|
   | 0 - 16 B        | two overlapping stores of 1, 2, 4 or 8 bytes, one at each end |
   | 17 - 64 B       | 4 or 8 words, half at each end                               |
   | 65 B and more   | loop of 4 words, aligned, ends stored overlapping            |
   | from rep_from   | rep stosb                                                    |

   rep_from is 512 B with ERMS, else never (as the SSE2 variant), from cpuid on the first call.
   i7-10750H (ERMS), cycles of rep stosb divided by the loop's, clang and gcc, aligned and +3:
   1.7 - 2.1 at 128 B, 1.09 - 1.37 at 256 B, 0.69 - 0.75 at 512 B, 0.23 - 0.47 from 4 KiB.
*/

#include "1-primitives/memory/impl/fill_bytes.hpp"

#include "1-primitives/atomic/atomic.hpp"
#include "1-primitives/atomic/memory_order.hpp"
#include "1-primitives/config/architecture.hpp"
#include "1-primitives/config/compiler.hpp"
#include "1-primitives/integers/u16.hpp"
#include "1-primitives/integers/u32.hpp"
#include "1-primitives/integers/u64.hpp"
#include "1-primitives/integers/u8.hpp"
#include "1-primitives/integers/usz.hpp"
#include "1-primitives/memory/impl/cpu_features.hpp"
#include "1-primitives/memory/impl/loose_bytes.hpp"

#if ca_architecture(x86_64) && !ca_target_feature(sse2)

#if ca_compiler(clang)
#	pragma clang attribute push(__attribute__((no_builtin)), apply_to = function)
#else
#	pragma GCC optimize("no-tree-loop-distribute-patterns")
#endif

static constinit ca::atomic<ca::usz> rep_from{static_cast<ca::usz>(-1)}; // file-state: ignore


// -- ACCESS -----------------------------------------------------------------

template <typename T>
[[gnu::always_inline]] static inline auto put(char* const to, const T value) noexcept -> void {
	ca::impl::loose<T>(to)._value = value;
}

[[gnu::always_inline]] static inline auto put_aligned(char* const to, const ca::u64 value) noexcept -> void {
	reinterpret_cast<ca::impl::aligned_bytes<ca::u64>*>(to)->_value = value;
}

static auto address(const void* const pointer) noexcept -> ca::usz {
	return reinterpret_cast<ca::usz>(pointer);
}


// -- SIZES ------------------------------------------------------------------

/* Two pieces of T, one at each end: count in [sizeof(T), 2 sizeof(T)]. */
template <typename T>
[[gnu::always_inline]] static inline auto fill_two(char* const to, const T value, const ca::usz count) noexcept -> void {

	put<T>(to, value);
	put<T>(to + count - sizeof(T), value);
}

/* 0 - 16 bytes. */
[[gnu::always_inline]] static inline auto fill_ladder(char* const to, const ca::u64 pattern, const ca::usz count) noexcept -> void {

	if (count >= 8U) [[unlikely]]
		fill_two<ca::u64>(to, pattern, count);
	else if (count >= 4U) [[unlikely]]
		fill_two<ca::u32>(to, static_cast<ca::u32>(pattern), count);
	else if (count >= 2U) [[unlikely]]
		fill_two<ca::u16>(to, static_cast<ca::u16>(pattern), count);
	else if (count != 0U)
		*to = static_cast<char>(pattern);
}

/* 16 - 32 bytes. */
[[gnu::always_inline]] static inline auto fill_four(char* const to, const ca::u64 pattern, const ca::usz count) noexcept -> void {

	put(to, pattern);
	put(to + 8U, pattern);
	put(to + count - 16U, pattern);
	put(to + count - 8U, pattern);
}

/* 32 - 64 bytes. */
[[gnu::always_inline]] static inline auto fill_eight(char* const to, const ca::u64 pattern, const ca::usz count) noexcept -> void {

	put(to, pattern);
	put(to + 8U, pattern);
	put(to + 16U, pattern);
	put(to + 24U, pattern);
	put(to + count - 32U, pattern);
	put(to + count - 24U, pattern);
	put(to + count - 16U, pattern);
	put(to + count - 8U, pattern);
}

/* More than 64 bytes. */
[[gnu::always_inline]] static inline auto fill_loop(char* const to, const ca::u64 pattern, const ca::usz count) noexcept -> void {

	char* const end = to + count;
	char* const stop = end - 32U;
	put(to, pattern);
	for (char* at = to + 8U - (address(to) & 7U); at < stop; at += 32U) {
		put_aligned(at, pattern);
		put_aligned(at + 8U, pattern);
		put_aligned(at + 16U, pattern);
		put_aligned(at + 24U, pattern);
	}
	put(stop, pattern);
	put(stop + 8U, pattern);
	put(stop + 16U, pattern);
	put(end - 8U, pattern);
}

[[gnu::always_inline]] static inline auto fill_repeat(char* to, const int value, ca::usz count) noexcept -> void {
	asm volatile("rep stosb" : "+D"(to), "+c"(count) : "a"(value) : "memory");
}


// -- VARIANT ----------------------------------------------------------------

static auto fill_integer(void* const target, const int value, const ca::usz count) noexcept -> void* {

	char* const to = static_cast<char*>(target);
	const ca::u64 pattern = static_cast<ca::u8>(value) * 0x0101010101010101U;
	if (count <= 16U) [[likely]]
		fill_ladder(to, pattern, count);
	else if (count <= 32U)
		fill_four(to, pattern, count);
	else if (count <= 64U)
		fill_eight(to, pattern, count);
	else if (count >= rep_from.load<ca::memory_order::relaxed>()) [[unlikely]]
		fill_repeat(to, value, count);
	else
		fill_loop(to, pattern, count);
	return target;
}

static auto resolve(void* const to, const int value, const ca::usz count) noexcept -> void* {

	rep_from.store<ca::memory_order::relaxed>(ca::impl::read_cpu_features()._erms ? 512U : static_cast<ca::usz>(-1));
	ca::impl::fill_bytes_variant.store<ca::memory_order::relaxed>(&fill_integer);
	return fill_integer(to, value, count);
}


// -- FILL BYTES VARIANT -----------------------------------------------------

constinit ca::atomic<ca::impl::fill_bytes_function> ca::impl::fill_bytes_variant{&resolve}; // ns-leak: ignore


// -- C SYMBOLS --------------------------------------------------------------

extern "C" {

	auto memset(void* const to, const int value, const ca::usz count) noexcept -> void* { // ns-leak: ignore
		return ca::impl::fill_bytes_variant.load<ca::memory_order::relaxed>()(to, value, count);
	}

} // extern "C"

#if ca_compiler(clang)
#	pragma clang attribute pop
#endif

#endif
