#ifndef ca_primitives_memory_impl_move_bytes_hpp
#define ca_primitives_memory_impl_move_bytes_hpp

#include "1-primitives/atomic/atomic.hpp"
#include "1-primitives/integers/usz.hpp"


// -- CA IMPL NAMESPACE ------------------------------------------------------

namespace ca::impl {


	// -- MOVE BYTES FUNCTION ------------------------------------------------

	/* @DOC Signature of a move_bytes variant: memmove semantics, count 0 accepts any pointers;
	   returns `to`, so memcpy and memmove are one jump through the variant. */
	using move_bytes_function = auto (*)(void*, const void*, ca::usz) noexcept -> void*;


	// -- MOVE BYTES VARIANT -------------------------------------------------

	/* @DOC The move_bytes variant of this CPU. Starts on a resolver that runs cpuid on the first
	   call, stores the variant here and forwards to it. x86-64: SSE2 or AVX2; each copies up to
	   8 vectors with overlapping loads and stores, more with an aligned 4-vector loop, rep movsb
	   from 8 KiB with ERMS and non-temporal stores from half the last-level cache. An
	   integer-only build (no SSE2: -mgeneral-regs-only, -mno-sse) moves 64-bit words, rep movsb
	   from 512 bytes with ERMS, and touches no vector register. Calls no libc. On x86-64 the library defines the C memcpy and memmove as one jump through it:
	   the calls copy_bytes and move_bytes leave to the compiler land there. */
	extern ca::atomic<ca::impl::move_bytes_function> move_bytes_variant;

} // namespace ca::impl

#endif // ca_primitives_memory_impl_move_bytes_hpp
