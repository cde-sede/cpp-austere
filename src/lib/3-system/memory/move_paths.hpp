#ifndef ca_system_memory_move_paths_hpp
#define ca_system_memory_move_paths_hpp

#include "1-primitives/config/architecture.hpp"
#include "1-primitives/integers/usz.hpp"
#include "1-primitives/memory/impl/loose_bytes.hpp"
#include "1-primitives/memory/impl/move_paths.hpp"
#include "1-primitives/simd/vec.hpp"
#include "2-vocabulary/failure.hpp"
#include "2-vocabulary/result.hpp"
#include "3-system/error/errc.hpp"


// -- CA NAMESPACE -----------------------------------------------------------

namespace ca {


	// -- MOVE PATHS ---------------------------------------------------------

	namespace impl {

		/* @DOC What a path returns for a size outside its class, before moving anything. */
		[[nodiscard, gnu::always_inline]] inline auto move_size_out_of_range(void) noexcept -> ca::result<void> {
			return ca::failure{ca::errc::out_of_range};
		}

	} // namespace impl


	/* @DOC The paths ca's memcpy and memmove take, one function per size class and instruction
	   set, for code that knows its sizes or its CPU and wants no dispatch. Each has memmove's
	   arguments; a count outside the path's class moves nothing and fails with
	   errc::out_of_range (any takes every count and cannot fail). All move overlapping ranges
	   correctly unless they say otherwise. This namespace is the x86-64 baseline (SSE2, every
	   x86-64 CPU; the size classes up to 128 bytes are portable vector code elsewhere); avx2
	   needs a CPU with AVX2, which the caller checks. Size classes are inline. memcpy, memmove,
	   copy_bytes and move_bytes remain the right default: they pick for you, and the compiler
	   sees through the builtins. */
	namespace move_paths {

		/* @DOC 0 - 15 bytes: two overlapping 8-, 4- or 2-byte integers, or one byte. */
		[[nodiscard, gnu::always_inline]] inline auto bytes_0_to_15(void* const to, const void* const from, const ca::usz count) noexcept -> ca::result<void> {

			if (count > 15U) [[unlikely]]
				return ca::impl::move_size_out_of_range();
			ca::impl::move_ladder(static_cast<char*>(to), static_cast<const char*>(from), count);
			return ca::result<void>{};
		}

		/* @DOC 16 - 32 bytes: two 16-byte vectors. */
		[[nodiscard, gnu::always_inline]] inline auto bytes_16_to_32(void* const to, const void* const from, const ca::usz count) noexcept -> ca::result<void> {

			if (count < 16U || count > 32U) [[unlikely]]
				return ca::impl::move_size_out_of_range();
			ca::impl::move_two<ca::simd::vec<char, 16U>>(static_cast<char*>(to), static_cast<const char*>(from), count);
			return ca::result<void>{};
		}

		/* @DOC 32 - 64 bytes: four 16-byte vectors. */
		[[nodiscard, gnu::always_inline]] inline auto bytes_32_to_64(void* const to, const void* const from, const ca::usz count) noexcept -> ca::result<void> {

			if (count < 32U || count > 64U) [[unlikely]]
				return ca::impl::move_size_out_of_range();
			ca::impl::move_four<ca::simd::vec<char, 16U>>(static_cast<char*>(to), static_cast<const char*>(from), count);
			return ca::result<void>{};
		}

		/* @DOC 64 - 128 bytes: eight 16-byte vectors. */
		[[nodiscard, gnu::always_inline]] inline auto bytes_64_to_128(void* const to, const void* const from, const ca::usz count) noexcept -> ca::result<void> {

			if (count < 64U || count > 128U) [[unlikely]]
				return ca::impl::move_size_out_of_range();
			ca::impl::move_eight<ca::simd::vec<char, 16U>>(static_cast<char*>(to), static_cast<const char*>(from), count);
			return ca::result<void>{};
		}

		/* @DOC 0 - 64 bytes, the class picked by size: what the C memcpy and memmove do in place. */
		[[nodiscard, gnu::always_inline]] inline auto bytes_0_to_64(void* const to, const void* const from, const ca::usz count) noexcept -> ca::result<void> {

			if (count > 64U) [[unlikely]]
				return ca::impl::move_size_out_of_range();
			ca::impl::move_few(static_cast<char*>(to), static_cast<const char*>(from), count);
			return ca::result<void>{};
		}

#if ca_architecture(x86_64) && ca_target_feature(sse2)

		/* @DOC More than 128 bytes forward, 64 at a time, stores aligned: to at or below from, or
		   no overlap. */
		[[nodiscard]] inline auto forward(void* const to, const void* const from, const ca::usz count) noexcept -> ca::result<void> {

			if (count <= 128U) [[unlikely]]
				return ca::impl::move_size_out_of_range();
			ca::impl::paths::forward(to, from, count);
			return ca::result<void>{};
		}

		/* @DOC More than 128 bytes backward, 64 at a time, stores aligned: to at or above from, or
		   no overlap. */
		[[nodiscard]] inline auto backward(void* const to, const void* const from, const ca::usz count) noexcept -> ca::result<void> {

			if (count <= 128U) [[unlikely]]
				return ca::impl::move_size_out_of_range();
			ca::impl::paths::backward(to, from, count);
			return ca::result<void>{};
		}

		/* @DOC More than 128 bytes with rep movsb, destination aligned to 64 first. No overlap; fast
		   only on CPUs with ERMS, from a few KiB. */
		[[nodiscard]] inline auto repeat(void* const to, const void* const from, const ca::usz count) noexcept -> ca::result<void> {

			if (count <= 128U) [[unlikely]]
				return ca::impl::move_size_out_of_range();
			ca::impl::paths::repeat(to, from, count);
			return ca::result<void>{};
		}

		/* @DOC More than 128 bytes with non-temporal stores, past the caches: for copies larger
		   than half the last-level cache that are not read again soon. No overlap. */
		[[nodiscard]] inline auto stream(void* const to, const void* const from, const ca::usz count) noexcept -> ca::result<void> {

			if (count <= 128U) [[unlikely]]
				return ca::impl::move_size_out_of_range();
			ca::impl::paths::stream(to, from, count);
			return ca::result<void>{};
		}

		/* @DOC More than 128 bytes: forward, backward, repeat or stream, as memcpy picks them (the
		   thresholds come from the CPU's caches, read on first use). */
		[[nodiscard]] inline auto large(void* const to, const void* const from, const ca::usz count) noexcept -> ca::result<void> {

			if (count <= 128U) [[unlikely]]
				return ca::impl::move_size_out_of_range();
			ca::impl::paths::large(to, from, count);
			return ca::result<void>{};
		}

		/* @DOC Any size: the whole SSE2 variant, called directly. */
		inline auto any(void* const to, const void* const from, const ca::usz count) noexcept -> void {
			ca::impl::paths::any(to, from, count);
		}


		/* @DOC The same paths with 32-byte AVX2 vectors; size classes double from 32 bytes. Only on
		   CPUs with AVX2. Size classes inline into AVX2 code (-mavx2, or a target("avx2")
		   function), else they are a direct call. Below 32 bytes AVX2 adds nothing: use the
		   baseline classes. */
		namespace avx2 {

			/* @DOC 32 - 64 bytes: two 32-byte vectors. */
			[[nodiscard, gnu::target("avx2")]] inline auto bytes_32_to_64(void* const to, const void* const from, const ca::usz count) noexcept -> ca::result<void> {

				if (count < 32U || count > 64U) [[unlikely]]
					return ca::impl::move_size_out_of_range();
				ca::impl::move_two<ca::simd::vec<char, 32U>>(static_cast<char*>(to), static_cast<const char*>(from), count);
				return ca::result<void>{};
			}

			/* @DOC 64 - 128 bytes: four 32-byte vectors. */
			[[nodiscard, gnu::target("avx2")]] inline auto bytes_64_to_128(void* const to, const void* const from, const ca::usz count) noexcept -> ca::result<void> {

				if (count < 64U || count > 128U) [[unlikely]]
					return ca::impl::move_size_out_of_range();
				ca::impl::move_four<ca::simd::vec<char, 32U>>(static_cast<char*>(to), static_cast<const char*>(from), count);
				return ca::result<void>{};
			}

			/* @DOC 128 - 256 bytes: eight 32-byte vectors. */
			[[nodiscard, gnu::target("avx2")]] inline auto bytes_128_to_256(void* const to, const void* const from, const ca::usz count) noexcept -> ca::result<void> {

				if (count < 128U || count > 256U) [[unlikely]]
					return ca::impl::move_size_out_of_range();
				ca::impl::move_eight<ca::simd::vec<char, 32U>>(static_cast<char*>(to), static_cast<const char*>(from), count);
				return ca::result<void>{};
			}

			/* @DOC More than 256 bytes forward, 128 at a time: to at or below from, or no overlap. */
			[[nodiscard]] inline auto forward(void* const to, const void* const from, const ca::usz count) noexcept -> ca::result<void> {

				if (count <= 256U) [[unlikely]]
					return ca::impl::move_size_out_of_range();
				ca::impl::paths::avx2::forward(to, from, count);
				return ca::result<void>{};
			}

			/* @DOC More than 256 bytes backward, 128 at a time: to at or above from, or no overlap. */
			[[nodiscard]] inline auto backward(void* const to, const void* const from, const ca::usz count) noexcept -> ca::result<void> {

				if (count <= 256U) [[unlikely]]
					return ca::impl::move_size_out_of_range();
				ca::impl::paths::avx2::backward(to, from, count);
				return ca::result<void>{};
			}

			/* @DOC More than 256 bytes with rep movsb. No overlap. */
			[[nodiscard]] inline auto repeat(void* const to, const void* const from, const ca::usz count) noexcept -> ca::result<void> {

				if (count <= 256U) [[unlikely]]
					return ca::impl::move_size_out_of_range();
				ca::impl::paths::avx2::repeat(to, from, count);
				return ca::result<void>{};
			}

			/* @DOC More than 256 bytes with non-temporal stores. No overlap. */
			[[nodiscard]] inline auto stream(void* const to, const void* const from, const ca::usz count) noexcept -> ca::result<void> {

				if (count <= 256U) [[unlikely]]
					return ca::impl::move_size_out_of_range();
				ca::impl::paths::avx2::stream(to, from, count);
				return ca::result<void>{};
			}

			/* @DOC More than 256 bytes, the strategy picked as memcpy does. */
			[[nodiscard]] inline auto large(void* const to, const void* const from, const ca::usz count) noexcept -> ca::result<void> {

				if (count <= 256U) [[unlikely]]
					return ca::impl::move_size_out_of_range();
				ca::impl::paths::avx2::large(to, from, count);
				return ca::result<void>{};
			}

			/* @DOC Any size: the whole AVX2 variant, called directly. */
			inline auto any(void* const to, const void* const from, const ca::usz count) noexcept -> void {
				ca::impl::paths::avx2::any(to, from, count);
			}

		} // namespace avx2

#endif

	} // namespace move_paths

} // namespace ca

#endif // ca_system_memory_move_paths_hpp
