#ifndef ca_primitives_memory_impl_find_bytes_hpp
#define ca_primitives_memory_impl_find_bytes_hpp

#include "1-primitives/atomic/atomic.hpp"
#include "1-primitives/config/architecture.hpp"
#include "1-primitives/integers/usz.hpp"


// -- CA IMPL NAMESPACE ------------------------------------------------------

namespace ca::impl {


	// -- FIND BYTE FUNCTION -------------------------------------------------

	/* @DOC Signature of a find_byte variant: memchr semantics, count 0 accepts any pointer. */
	using find_byte_function = auto (*)(const void*, int, ca::usz) noexcept -> void*;

	/* @DOC Signature of a string_length variant: strlen semantics. */
	using string_length_function = auto (*)(const char*) noexcept -> ca::usz;


	// -- FIND BYTE VARIANT --------------------------------------------------

	/* @DOC The find_byte and string_length variants of this CPU, picked by cpuid on the first
	   call as move_bytes_variant. x86-64: SSE2 or AVX2; aligned vector loads only (one never
	   crosses a page, so the bytes around the range are read, never faulted on), 4 vectors per
	   test past the first; an integer-only build, aligned 64-bit words (SWAR). Call no libc. On x86-64 the library defines the C memchr and strlen
	   as one jump through them. */
	extern ca::atomic<ca::impl::find_byte_function> find_byte_variant;

	extern ca::atomic<ca::impl::string_length_function> string_length_variant;

#if ca_architecture(x86_64) && ca_target_feature(sse2)

	/* @DOC The variants find_byte_variant and string_length_variant pick from, for tests: every
	   x86-64 CPU runs the sse2 ones, the avx2 ones need AVX2 and BMI2. */
	auto find_byte_sse2(const void* data, int value, ca::usz count) noexcept -> void*;

	[[gnu::target("avx2,bmi2")]] auto find_byte_avx2(const void* data, int value, ca::usz count) noexcept -> void*;

	auto string_length_sse2(const char* text) noexcept -> ca::usz;

	[[gnu::target("avx2,bmi2")]] auto string_length_avx2(const char* text) noexcept -> ca::usz;

#endif

} // namespace ca::impl

#endif // ca_primitives_memory_impl_find_bytes_hpp
