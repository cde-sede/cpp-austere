/* memchr and strlen for an integer-only x86-64 build (-mgeneral-regs-only, -mno-sse): eight
   bytes at a time in a 64-bit word (SWAR), no vector register touched. Loads are aligned words
   in asm, as ca::simd::peek_aligned: one never crosses a page, so the bytes around the range
   are read, never faulted on, and the compiler sees no access beyond the range. A byte matches
   when its high bit is set in zero_bytes(word ^ pattern), exact per byte (no borrow from
   another), so the bytes before the range are masked off and the lowest bit left is the first
   match.
*/

#include "1-primitives/memory/impl/find_bytes.hpp"

#include "1-primitives/atomic/memory_order.hpp"
#include "1-primitives/bit/countr_zero.hpp"
#include "1-primitives/config/architecture.hpp"
#include "1-primitives/config/compiler.hpp"
#include "1-primitives/integers/u64.hpp"
#include "1-primitives/integers/u8.hpp"
#include "1-primitives/integers/usz.hpp"

#if ca_architecture(x86_64) && !ca_target_feature(sse2)

#if ca_compiler(clang)
#	pragma clang attribute push(__attribute__((no_builtin)), apply_to = function)
#else
#	pragma GCC optimize("no-tree-loop-distribute-patterns")
#endif

static constexpr ca::u64 LOW_SEVEN = 0x7f7f7f7f7f7f7f7fU;


// -- ACCESS -----------------------------------------------------------------

/* The aligned word at at. */
[[gnu::always_inline]] static inline auto peek_word(const char* const at) noexcept -> ca::u64 {

	ca::u64 word;
	asm("movq %1, %0" : "=r"(word) : "m"(*reinterpret_cast<const char(*)[8]>(at)));
	return word;
}

/* The high bit of each byte of word that is 0. */
[[gnu::always_inline]] static inline auto zero_bytes(const ca::u64 word) noexcept -> ca::u64 {
	return ~(((word & LOW_SEVEN) + LOW_SEVEN) | word | LOW_SEVEN);
}

static auto address(const void* const pointer) noexcept -> ca::usz {
	return reinterpret_cast<ca::usz>(pointer);
}


// -- FIND -------------------------------------------------------------------

static auto find_integer(const void* const data, const int value, const ca::usz count) noexcept -> void* {

	if (count == 0U) [[unlikely]]
		return nullptr;
	const char* const start = static_cast<const char*>(data);
	const ca::usz offset = address(start) & 7U;
	const char* const at = start - offset;
	const ca::usz limit = count + offset;
	const ca::u64 pattern = static_cast<ca::u8>(value) * 0x0101010101010101U;
	ca::u64 found = zero_bytes(peek_word(at) ^ pattern) & (~ca::u64{0} << (8U * offset));
	ca::usz seen = 8U;
	while (found == 0U) {
		if (seen >= limit)
			return nullptr;
		found = zero_bytes(peek_word(at + seen) ^ pattern);
		seen += 8U;
	}
	const ca::usz index = seen - 8U + static_cast<ca::usz>(ca::countr_zero(found)) / 8U;
	return index < limit ? const_cast<char*>(at + index) : nullptr;
}

static auto length_integer(const char* const text) noexcept -> ca::usz {

	const ca::usz offset = address(text) & 7U;
	const char* at = text - offset;
	ca::u64 found = zero_bytes(peek_word(at)) & (~ca::u64{0} << (8U * offset));
	while (found == 0U) {
		at += 8U;
		found = zero_bytes(peek_word(at));
	}
	return static_cast<ca::usz>(at + ca::countr_zero(found) / 8 - text);
}


// -- FIND BYTE VARIANT ------------------------------------------------------

constinit ca::atomic<ca::impl::find_byte_function> ca::impl::find_byte_variant{&find_integer}; // ns-leak: ignore

constinit ca::atomic<ca::impl::string_length_function> ca::impl::string_length_variant{&length_integer}; // ns-leak: ignore


// -- C SYMBOLS --------------------------------------------------------------

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
