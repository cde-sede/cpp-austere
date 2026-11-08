#ifndef ca_primitives_simd_peek_aligned_hpp
#define ca_primitives_simd_peek_aligned_hpp

#include "1-primitives/config/architecture.hpp"


// -- CA SIMD NAMESPACE ------------------------------------------------------

namespace ca::simd {


	// -- PEEK ALIGNED -------------------------------------------------------

#if ca_architecture(x86_64) && ca_target_feature(sse2)

	/* @DOC ca::simd::peek at p aligned to sizeof(V): such a load never crosses a page, so it may
	   cover bytes before and after any range inside it with no check. x86-64 with SSE2 only. */
	template <typename V>
	requires (sizeof(V) == 16U)
	[[gnu::always_inline]] inline auto peek_aligned(const void* const p, V& v) noexcept -> void {
		asm("movdqa %1, %0" : "=x"(v) : "m"(*static_cast<const char(*)[16]>(p)));
	}

	template <typename V>
	requires (sizeof(V) == 32U)
	[[gnu::target("avx2")]] inline auto peek_aligned(const void* const p, V& v) noexcept -> void {
		asm("vmovdqa %1, %0" : "=x"(v) : "m"(*static_cast<const char(*)[32]>(p)));
	}

#endif

} // namespace ca::simd

#endif // ca_primitives_simd_peek_aligned_hpp
