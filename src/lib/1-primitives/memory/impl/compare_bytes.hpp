#ifndef ca_primitives_memory_impl_compare_bytes_hpp
#define ca_primitives_memory_impl_compare_bytes_hpp

#include "1-primitives/atomic/atomic.hpp"
#include "1-primitives/config/architecture.hpp"
#include "1-primitives/integers/usz.hpp"


// -- CA IMPL NAMESPACE ------------------------------------------------------

namespace ca::impl {


	// -- COMPARE BYTES FUNCTION ---------------------------------------------

	/* @DOC Signature of a compare_bytes variant (memcmp: negative, 0 or positive, bytes as
	   unsigned) and of a differ_bytes one (bcmp: 0 when equal, nonzero otherwise). Count 0
	   accepts any pointers. */
	using compare_bytes_function = auto (*)(const void*, const void*, ca::usz) noexcept -> int;


	// -- COMPARE BYTES VARIANT ----------------------------------------------

	/* @DOC The compare_bytes variant of this CPU, picked by cpuid on the first call as
	   move_bytes_variant. x86-64: SSE2 or AVX2 with BMI2; below one vector a masked compare,
	   up to 8 vectors overlapping, more with a 4-vector loop; the first difference by bit scan.
	   An integer-only build compares 64-bit words, the first difference by byte swap. Calls no
	   libc. On x86-64 the library defines the C memcmp as one jump through it. */
	extern ca::atomic<ca::impl::compare_bytes_function> compare_bytes_variant;


	// -- DIFFER BYTES VARIANT -----------------------------------------------

	/* @DOC The differ_bytes variant of this CPU: compare_bytes_variant without locating the
	   difference. clang turns memcmp(...) == 0 into bcmp; on x86-64 the library defines the C
	   bcmp as one jump through it. */
	extern ca::atomic<ca::impl::compare_bytes_function> differ_bytes_variant;

#if ca_architecture(x86_64) && ca_target_feature(sse2)

	/* @DOC The variants compare_bytes_variant and differ_bytes_variant pick from, for tests:
	   every x86-64 CPU runs the sse2 ones, the avx2 ones need AVX2 and BMI2. */
	auto compare_bytes_sse2(const void* a, const void* b, ca::usz count) noexcept -> int;

	[[gnu::target("avx2,bmi2")]] auto compare_bytes_avx2(const void* a, const void* b, ca::usz count) noexcept -> int;

	auto differ_bytes_sse2(const void* a, const void* b, ca::usz count) noexcept -> int;

	[[gnu::target("avx2,bmi2")]] auto differ_bytes_avx2(const void* a, const void* b, ca::usz count) noexcept -> int;

#endif

} // namespace ca::impl

#endif // ca_primitives_memory_impl_compare_bytes_hpp
