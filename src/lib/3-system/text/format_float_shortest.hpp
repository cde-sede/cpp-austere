#ifndef ca_system_text_format_float_shortest_hpp
#define ca_system_text_format_float_shortest_hpp

#include "1-primitives/floats/f32.hpp"
#include "1-primitives/floats/f64.hpp"
#include "1-primitives/integers/usz.hpp"
#include "2-vocabulary/span.hpp"


// -- CA NAMESPACE -----------------------------------------------------------

namespace ca {


	// -- SYS ----------------------------------------------------------------

	namespace sys {


		// -- FORMAT FLOAT SHORTEST ------------------------------------------

		/* @DOC Writes the fewest %g digits that read back as the same value: 0.1 is "0.1", not
		   "0.10000000000000001"; an f32 round-trips as f32. Into buffer; returns the length, at
		   most buffer.size() - 1, and NUL-terminates (buffer not empty). Ryu: a few 128-bit
		   products, laid out as %.Pg with P = max(digits, 15) (f32: 6). Not defined in an
		   integer-only build (ca_target_feature(floats) 0). */
		auto format_float_shortest(ca::span<char> buffer, ca::f64 value) noexcept -> ca::usz;

		auto format_float_shortest(ca::span<char> buffer, ca::f32 value) noexcept -> ca::usz;

	} // namespace sys

} // namespace ca

#endif // ca_system_text_format_float_shortest_hpp
