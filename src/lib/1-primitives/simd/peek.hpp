#ifndef ca_primitives_simd_peek_hpp
#define ca_primitives_simd_peek_hpp

#include "1-primitives/config/architecture.hpp"


// -- CA SIMD NAMESPACE ------------------------------------------------------

namespace ca::simd {


	// -- PEEK ---------------------------------------------------------------

#if ca_architecture(x86_64) && ca_target_feature(sse2)

	/* @DOC The 16 or 32 bytes at p into v, any alignment, in asm: the compiler sees no access
	   beyond the range the caller owns, which the load may pass, never beyond p's page (the
	   caller checks: p's offset in its 4 KiB page at most 4096 - sizeof(V)). The 16-byte one is
	   legacy SSE (AVX state is clean at entry), the 32-byte one AVX2 code. x86-64 with SSE2
	   only. */
	template <typename V>
	requires (sizeof(V) == 16U)
	[[gnu::always_inline]] inline auto peek(const void* const p, V& v) noexcept -> void {
		asm("movdqu %1, %0" : "=x"(v) : "m"(*static_cast<const char(*)[16]>(p)));
	}

	template <typename V>
	requires (sizeof(V) == 32U)
	[[gnu::target("avx2")]] inline auto peek(const void* const p, V& v) noexcept -> void {
		asm("vmovdqu %1, %0" : "=x"(v) : "m"(*static_cast<const char(*)[32]>(p)));
	}

#endif

} // namespace ca::simd

#endif // ca_primitives_simd_peek_hpp
