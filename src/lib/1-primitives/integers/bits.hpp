#ifndef ca_primitives_integers_bits_hpp
#define ca_primitives_integers_bits_hpp

#include "1-primitives/integers/byte.hpp"
#include "1-primitives/integers/usz.hpp"


// -- CA NAMESPACE -----------------------------------------------------------

namespace ca {


	// -- BITS ---------------------------------------------------------------

	namespace impl {

		consteval auto count_byte_bits(void) noexcept -> ca::usz {
			ca::usz n = 0U;
			for (ca::byte b = 1U; b != 0U; b = static_cast<ca::byte>(b << 1U))
				++n;
			return n;
		}

	} // namespace impl


	/* @DOC Bits in a byte, deduced by shifting a set bit out of ca::byte. */
	inline constexpr ca::usz byte_bits = ca::impl::count_byte_bits();

	/* @DOC Bits in the object representation of T. */
	template <typename T>
	inline constexpr ca::usz bits = sizeof(T) * ca::byte_bits;

} // namespace ca

#endif // ca_primitives_integers_bits_hpp
