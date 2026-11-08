#ifndef ca_primitives_memory_impl_fill_bytes_hpp
#define ca_primitives_memory_impl_fill_bytes_hpp

#include "1-primitives/atomic/atomic.hpp"
#include "1-primitives/config/architecture.hpp"
#include "1-primitives/integers/usz.hpp"


// -- CA IMPL NAMESPACE ------------------------------------------------------

namespace ca::impl {


	// -- FILL BYTES FUNCTION ------------------------------------------------

	/* @DOC Signature of a fill_bytes variant: memset semantics, count 0 accepts any pointer;
	   returns `to`, so memset is one jump through the variant. */
	using fill_bytes_function = auto (*)(void*, int, ca::usz) noexcept -> void*;


	// -- FILL BYTES VARIANT -------------------------------------------------

	/* @DOC The fill_bytes variant of this CPU, picked by cpuid on the first call as
	   move_bytes_variant. x86-64: SSE2 or AVX2; each stores up to 8 vectors overlapping, more
	   with an aligned 4-vector loop, rep stosb from 32 KiB with ERMS. An integer-only build
	   stores 64-bit words, rep stosb from 512 bytes with ERMS. Calls no libc. On x86-64
	   the library defines the C memset as one jump through it. */
	extern ca::atomic<ca::impl::fill_bytes_function> fill_bytes_variant;

#if ca_architecture(x86_64) && ca_target_feature(sse2)

	/* @DOC The variants fill_bytes_variant picks from, for tests: every x86-64 CPU runs
	   fill_bytes_sse2, fill_bytes_avx2 needs AVX2. */
	auto fill_bytes_sse2(void* to, int value, ca::usz count) noexcept -> void*;

	[[gnu::target("avx2")]] auto fill_bytes_avx2(void* to, int value, ca::usz count) noexcept -> void*;

#endif

} // namespace ca::impl

#endif // ca_primitives_memory_impl_fill_bytes_hpp
