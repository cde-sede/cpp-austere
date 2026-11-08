#ifndef ca_primitives_memory_impl_move_paths_hpp
#define ca_primitives_memory_impl_move_paths_hpp

#include "1-primitives/config/architecture.hpp"
#include "1-primitives/integers/u16.hpp"
#include "1-primitives/integers/u32.hpp"
#include "1-primitives/integers/u64.hpp"
#include "1-primitives/integers/usz.hpp"
#include "1-primitives/memory/impl/loose_bytes.hpp"
#include "1-primitives/simd/vec.hpp"


// -- CA NAMESPACE -----------------------------------------------------------

namespace ca {


	// -- MOVE PATHS ---------------------------------------------------------

	namespace impl {

		/* @DOC Two pieces of T, one from each end: count in [sizeof(T), 2 sizeof(T)]. Every path
		   loads all it moves before storing, so overlapping ranges are fine. */
		template <typename T>
		[[gnu::always_inline]] inline auto move_two(char* const to, const char* const from, const ca::usz count) noexcept -> void {

			const T a = ca::impl::loose<T>(from)._value;
			const T b = ca::impl::loose<T>(from + count - sizeof(T))._value;
			ca::impl::loose<T>(to)._value = a;
			ca::impl::loose<T>(to + count - sizeof(T))._value = b;
		}

		/* @DOC Four vectors V, two from each end: count in [2 sizeof(V), 4 sizeof(V)]. */
		template <typename V>
		[[gnu::always_inline]] inline auto move_four(char* const to, const char* const from, const ca::usz count) noexcept -> void {

			constexpr ca::usz n = sizeof(V);
			const V a = ca::impl::loose<V>(from)._value;
			const V b = ca::impl::loose<V>(from + n)._value;
			const V c = ca::impl::loose<V>(from + count - 2U * n)._value;
			const V d = ca::impl::loose<V>(from + count - n)._value;
			ca::impl::loose<V>(to)._value = a;
			ca::impl::loose<V>(to + n)._value = b;
			ca::impl::loose<V>(to + count - 2U * n)._value = c;
			ca::impl::loose<V>(to + count - n)._value = d;
		}

		/* @DOC Eight vectors V, four from each end: count in [4 sizeof(V), 8 sizeof(V)]. */
		template <typename V>
		[[gnu::always_inline]] inline auto move_eight(char* const to, const char* const from, const ca::usz count) noexcept -> void {

			constexpr ca::usz n = sizeof(V);
			const V a = ca::impl::loose<V>(from)._value;
			const V b = ca::impl::loose<V>(from + n)._value;
			const V c = ca::impl::loose<V>(from + 2U * n)._value;
			const V d = ca::impl::loose<V>(from + 3U * n)._value;
			const V e = ca::impl::loose<V>(from + count - 4U * n)._value;
			const V f = ca::impl::loose<V>(from + count - 3U * n)._value;
			const V g = ca::impl::loose<V>(from + count - 2U * n)._value;
			const V h = ca::impl::loose<V>(from + count - n)._value;
			ca::impl::loose<V>(to)._value = a;
			ca::impl::loose<V>(to + n)._value = b;
			ca::impl::loose<V>(to + 2U * n)._value = c;
			ca::impl::loose<V>(to + 3U * n)._value = d;
			ca::impl::loose<V>(to + count - 4U * n)._value = e;
			ca::impl::loose<V>(to + count - 3U * n)._value = f;
			ca::impl::loose<V>(to + count - 2U * n)._value = g;
			ca::impl::loose<V>(to + count - n)._value = h;
		}

		/* @DOC 0 - 15 bytes: two overlapping integers of 8, 4 or 2 bytes, or one byte. */
		[[gnu::always_inline]] inline auto move_ladder(char* const to, const char* const from, const ca::usz count) noexcept -> void {

			if (count >= 8U) [[unlikely]]
				ca::impl::move_two<ca::u64>(to, from, count);
			else if (count >= 4U) [[unlikely]]
				ca::impl::move_two<ca::u32>(to, from, count);
			else if (count >= 2U) [[unlikely]]
				ca::impl::move_two<ca::u16>(to, from, count);
			else if (count != 0U)
				*to = *from;
		}

		/* @DOC 0 - 31 bytes: the ladder, or two 16-byte vectors from 16. */
		[[gnu::always_inline]] inline auto move_small(char* const to, const char* const from, const ca::usz count) noexcept -> void {

			if (count >= 16U) [[unlikely]]
				ca::impl::move_two<ca::simd::vec<char, 16U>>(to, from, count);
			else
				ca::impl::move_ladder(to, from, count);
		}

		/* @DOC 0 - 64 bytes with 16-byte vectors: what the C memcpy and memmove do in place. */
		[[gnu::always_inline]] inline auto move_few(char* const to, const char* const from, const ca::usz count) noexcept -> void {

			if (count < 16U) [[unlikely]] {
				ca::impl::move_ladder(to, from, count);
				return;
			}
			if (count <= 32U) [[likely]] {
				ca::impl::move_two<ca::simd::vec<char, 16U>>(to, from, count);
				return;
			}
			ca::impl::move_four<ca::simd::vec<char, 16U>>(to, from, count);
		}

#if ca_architecture(x86_64) && ca_target_feature(sse2)

		/* @DOC The out-of-line strategies of ca::move_paths, unchecked: the variants of
		   move_bytes_x86_64.cpp over 16-byte vectors (more than 128 bytes), and over 32-byte ones
		   in avx2 (more than 256). */
		namespace paths {

			auto forward(void* to, const void* from, ca::usz count) noexcept -> void;

			auto backward(void* to, const void* from, ca::usz count) noexcept -> void;

			auto repeat(void* to, const void* from, ca::usz count) noexcept -> void;

			auto stream(void* to, const void* from, ca::usz count) noexcept -> void;

			auto large(void* to, const void* from, ca::usz count) noexcept -> void;

			auto any(void* to, const void* from, ca::usz count) noexcept -> void;

			namespace avx2 {

				[[gnu::target("avx2")]] auto forward(void* to, const void* from, ca::usz count) noexcept -> void;

				[[gnu::target("avx2")]] auto backward(void* to, const void* from, ca::usz count) noexcept -> void;

				[[gnu::target("avx2")]] auto repeat(void* to, const void* from, ca::usz count) noexcept -> void;

				[[gnu::target("avx2")]] auto stream(void* to, const void* from, ca::usz count) noexcept -> void;

				[[gnu::target("avx2")]] auto large(void* to, const void* from, ca::usz count) noexcept -> void;

				[[gnu::target("avx2")]] auto any(void* to, const void* from, ca::usz count) noexcept -> void;

			} // namespace avx2

		} // namespace paths

#endif

	} // namespace impl

} // namespace ca

#endif // ca_primitives_memory_impl_move_paths_hpp
