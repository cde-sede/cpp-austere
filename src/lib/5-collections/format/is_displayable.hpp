#ifndef ca_collections_format_is_displayable_hpp
#define ca_collections_format_is_displayable_hpp

#include "5-collections/format/impl/display_kind.hpp"


// -- CA NAMESPACE -----------------------------------------------------------

namespace ca {


	// -- IS DISPLAYABLE -----------------------------------------------------

	/* @DOC Whether print and printf take a T: bool, char, standard integers, enums (as their
	   integer), f32, f64, C strings and char arrays, string views (ca::sv) and types with
	   `view() -> sv::basic<C>` (fs::path, ca::str), ca::error (the message its trace started with, else its ca::errc name, else the system's text; `{:d}` the code), object pointers, nullptr, and
	   types with `display(ca::text_writer&) const`. f32 and f64 are not in an integer-only build
	   (ca_target_feature(floats) 0). */
	template <typename T>
	concept is_displayable = ca::impl::display_kind_of<T> != ca::impl::display_kind::none;

} // namespace ca

#endif // ca_collections_format_is_displayable_hpp
