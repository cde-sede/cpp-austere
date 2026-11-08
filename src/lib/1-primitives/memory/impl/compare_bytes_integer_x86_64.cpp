/* memcmp and bcmp for an integer-only x86-64 build (-mgeneral-regs-only, -mno-sse): 64-bit
   words, no vector register touched. Below 8 bytes, two overlapping pieces of 4 or 2 bytes, one
   from each end, joined into one integer; from 8, 16 bytes per test (bcmp 32), the last word
   overlapping.
   memcmp orders the first differing words by their byte-swapped values: on a little-endian CPU
   the first differing byte is then the most significant one that differs.
*/

#include "1-primitives/memory/impl/compare_bytes.hpp"

#include "1-primitives/atomic/memory_order.hpp"
#include "1-primitives/bit/byteswap.hpp"
#include "1-primitives/config/architecture.hpp"
#include "1-primitives/config/compiler.hpp"
#include "1-primitives/integers/u16.hpp"
#include "1-primitives/integers/u32.hpp"
#include "1-primitives/integers/u64.hpp"
#include "1-primitives/integers/u8.hpp"
#include "1-primitives/integers/usz.hpp"
#include "1-primitives/memory/impl/loose_bytes.hpp"

#if ca_architecture(x86_64) && !ca_target_feature(sse2)

#if ca_compiler(clang)
#	pragma clang attribute push(__attribute__((no_builtin)), apply_to = function)
#else
#	pragma GCC optimize("no-tree-loop-distribute-patterns")
#endif


// -- ACCESS -----------------------------------------------------------------

template <typename T>
[[gnu::always_inline]] static inline auto load(const char* const at) noexcept -> T {
	return ca::impl::loose<T>(at)._value;
}

/* The 2 - 7 bytes at at as one integer, the first byte most significant: an order-preserving
   key. count in [sizeof(T), 2 sizeof(T)]. */
template <typename T, typename W>
[[gnu::always_inline]] static inline auto key(const char* const at, const ca::usz count) noexcept -> W {
	return static_cast<W>(static_cast<W>(ca::byteswap(load<T>(at))) << (8U * sizeof(T))) | ca::byteswap(load<T>(at + count - sizeof(T)));
}

[[gnu::always_inline]] static inline auto order(const ca::u64 a, const ca::u64 b) noexcept -> int {
	return ca::byteswap(a) < ca::byteswap(b) ? -1 : 1;
}


// -- COMPARE ----------------------------------------------------------------

static auto compare_integer(const void* const left, const void* const right, const ca::usz count) noexcept -> int {

	const char* const a = static_cast<const char*>(left);
	const char* const b = static_cast<const char*>(right);
	if (count < 8U) [[unlikely]] {
		if (count >= 4U) {
			const ca::u64 x = key<ca::u32, ca::u64>(a, count);
			const ca::u64 y = key<ca::u32, ca::u64>(b, count);
			return x == y ? 0 : x < y ? -1 : 1;
		}
		if (count >= 2U) {
			const ca::u32 x = key<ca::u16, ca::u32>(a, count);
			const ca::u32 y = key<ca::u16, ca::u32>(b, count);
			return x == y ? 0 : x < y ? -1 : 1;
		}
		return count != 0U ? static_cast<int>(static_cast<ca::u8>(*a)) - static_cast<int>(static_cast<ca::u8>(*b)) : 0;
	}
	ca::usz i = 0U;
	for (; i + 16U < count; i += 16U) {
		const ca::u64 a0 = load<ca::u64>(a + i);
		const ca::u64 b0 = load<ca::u64>(b + i);
		const ca::u64 a1 = load<ca::u64>(a + i + 8U);
		const ca::u64 b1 = load<ca::u64>(b + i + 8U);
		if (((a0 ^ b0) | (a1 ^ b1)) != 0U) [[unlikely]]
			return a0 != b0 ? order(a0, b0) : order(a1, b1);
	}
	if (i + 8U < count) {
		const ca::u64 a0 = load<ca::u64>(a + i);
		const ca::u64 b0 = load<ca::u64>(b + i);
		if (a0 != b0)
			return order(a0, b0);
	}
	const ca::u64 a1 = load<ca::u64>(a + count - 8U);
	const ca::u64 b1 = load<ca::u64>(b + count - 8U);
	return a1 == b1 ? 0 : order(a1, b1);
}

static auto differ_integer(const void* const left, const void* const right, const ca::usz count) noexcept -> int {

	const char* const a = static_cast<const char*>(left);
	const char* const b = static_cast<const char*>(right);
	if (count < 8U) [[unlikely]] {
		if (count >= 4U)
			return ((load<ca::u32>(a) ^ load<ca::u32>(b)) | (load<ca::u32>(a + count - 4U) ^ load<ca::u32>(b + count - 4U))) != 0U;
		if (count >= 2U)
			return ((load<ca::u16>(a) ^ load<ca::u16>(b)) | (load<ca::u16>(a + count - 2U) ^ load<ca::u16>(b + count - 2U))) != 0U;
		return count != 0U && *a != *b;
	}
	ca::usz i = 0U;
	for (; i + 32U < count; i += 32U) {
		const ca::u64 d0 = load<ca::u64>(a + i) ^ load<ca::u64>(b + i);
		const ca::u64 d1 = load<ca::u64>(a + i + 8U) ^ load<ca::u64>(b + i + 8U);
		const ca::u64 d2 = load<ca::u64>(a + i + 16U) ^ load<ca::u64>(b + i + 16U);
		const ca::u64 d3 = load<ca::u64>(a + i + 24U) ^ load<ca::u64>(b + i + 24U);
		if ((d0 | d1 | d2 | d3) != 0U) [[unlikely]]
			return 1;
	}
	ca::u64 d = 0U;
	for (; i + 8U < count; i += 8U)
		d |= load<ca::u64>(a + i) ^ load<ca::u64>(b + i);
	d |= load<ca::u64>(a + count - 8U) ^ load<ca::u64>(b + count - 8U);
	return d != 0U;
}


// -- COMPARE BYTES VARIANT --------------------------------------------------

constinit ca::atomic<ca::impl::compare_bytes_function> ca::impl::compare_bytes_variant{&compare_integer}; // ns-leak: ignore

constinit ca::atomic<ca::impl::compare_bytes_function> ca::impl::differ_bytes_variant{&differ_integer}; // ns-leak: ignore


// -- C SYMBOLS --------------------------------------------------------------

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
