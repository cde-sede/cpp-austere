#ifndef ca_system_text_format_float_hpp
#define ca_system_text_format_float_hpp

#include "1-primitives/floats/f64.hpp"
#include "1-primitives/integers/sint.hpp"
#include "1-primitives/integers/usz.hpp"
#include "2-vocabulary/span.hpp"


// -- CA NAMESPACE -----------------------------------------------------------

namespace ca {


	// -- SYS ----------------------------------------------------------------

	namespace sys {


		// -- FORMAT FLOAT ---------------------------------------------------

		/* @DOC Writes value as printf's %<conversion> would (conversion one of f F e E g G a A), with
		   precision when >= 0 and the # flag when alternate; no width, no + flag, into buffer.
		   Returns the length, at most buffer.size() - 1, and NUL-terminates (buffer not empty).
		   Exact digits, ties to even; always '.' as the decimal point. Not defined in an
		   integer-only build (ca_target_feature(floats) 0). */
		auto format_float(ca::span<char> buffer, ca::f64 value, char conversion, ca::sint precision, bool alternate) noexcept -> ca::usz;

	} // namespace sys

} // namespace ca

#endif // ca_system_text_format_float_hpp
