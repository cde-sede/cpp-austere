#ifndef ca_primitives_simd_mask_of_hpp
#define ca_primitives_simd_mask_of_hpp

#include "1-primitives/bit/bit_cast.hpp"
#include "1-primitives/config/architecture.hpp"
#include "1-primitives/integers/bits.hpp"
#include "1-primitives/integers/u32.hpp"
#include "1-primitives/integers/uint_of.hpp"
#include "1-primitives/integers/usz.hpp"
#include "1-primitives/simd/lane_of.hpp"
#include "1-primitives/simd/lanes.hpp"
#include "1-primitives/simd/mask.hpp"
#include "1-primitives/simd/vec.hpp"


// -- CA SIMD NAMESPACE ------------------------------------------------------

namespace ca::simd {


	// -- MASK OF ------------------------------------------------------------

	namespace impl {

		/* @DOC Bit i set when lane i of v has its high bit set, lane by lane: compile time, and the
		   lanes no instruction gathers. */
		template <typename V>
		[[nodiscard]] constexpr auto high_bits(const V& v) noexcept -> typename ca::simd::mask<ca::simd::lanes<V>>::word {

			using word = typename ca::simd::mask<ca::simd::lanes<V>>::word;
			using unit = ca::uint_of<ca::bits<ca::simd::lane_of<V>>>;
			struct units final {
					unit _values[ca::simd::lanes<V>];
			};
			const units each = ca::bit_cast<units>(v);
			word bits = 0U;
			for (ca::usz i = 0U; i < ca::simd::lanes<V>; ++i)
				bits |= static_cast<word>(each._values[i] >> (ca::bits<unit> - 1U)) << i;
			return bits;
		}

#if ca_architecture(x86_64) && ca_target_feature(sse2)

		/* @DOC pmovmskb over 16 byte lanes. */
		template <typename V>
		requires (sizeof(V) == 16U)
		[[nodiscard, gnu::always_inline]] inline auto byte_mask(const V& v) noexcept -> ca::u32 {
			return static_cast<ca::u32>(__builtin_ia32_pmovmskb128(__builtin_bit_cast(ca::simd::vec<char, 16U>, v)));
		}

		/* @DOC vpmovmskb over 32 byte lanes. Not always_inline: generic templates calling it are
		   compiled without AVX2; AVX2 callers flattened inline it. The mask is opaque: clang
		   otherwise tests it with vpxor and vptest (3 uops, then the compare again). */
		template <typename V>
		requires (sizeof(V) == 32U)
		[[nodiscard, gnu::target("avx2")]] inline auto byte_mask(const V& v) noexcept -> ca::u32 {

			ca::u32 bits = static_cast<ca::u32>(__builtin_ia32_pmovmskb256(__builtin_bit_cast(ca::simd::vec<char, 32U>, v)));
			asm("" : "+r"(bits));
			return bits;
		}

#endif

	} // namespace impl

	/* @DOC The lanes of v whose high bit is set: of a comparison (lanes of all ones or zeros),
	   the lanes where it holds. Byte lanes of 16 and 32 are one pmovmskb on x86-64 (32 in AVX2
	   code); other vectors, every one at compile time and in an integer-only build, are
	   gathered lane by lane. */
	template <typename V>
	requires (ca::simd::lanes<V> <= 64U)
	[[nodiscard, gnu::always_inline]] constexpr auto mask_of(const V& v) noexcept -> ca::simd::mask<ca::simd::lanes<V>> {

		using mask = ca::simd::mask<ca::simd::lanes<V>>;
		if consteval {
			return mask{ca::simd::impl::high_bits(v)};
		} else {
#if ca_architecture(x86_64) && ca_target_feature(sse2)
			if constexpr (sizeof(ca::simd::lane_of<V>) == 1U && (sizeof(V) == 16U || sizeof(V) == 32U))
				return mask{ca::simd::impl::byte_mask(v)};
			else
				return mask{ca::simd::impl::high_bits(v)};
#else
			return mask{ca::simd::impl::high_bits(v)};
#endif
		}
	}

} // namespace ca::simd

#endif // ca_primitives_simd_mask_of_hpp
