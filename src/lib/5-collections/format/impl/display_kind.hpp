#ifndef ca_collections_format_impl_display_kind_hpp
#define ca_collections_format_impl_display_kind_hpp

#include "1-primitives/config/architecture.hpp"
#include "1-primitives/integers/u8.hpp"
#include "1-primitives/integers/usz.hpp"
#include "1-primitives/traits/decay.hpp"
#include "1-primitives/traits/impl/is_any_of.hpp"
#include "1-primitives/traits/is_enum.hpp"
#include "1-primitives/traits/is_function.hpp"
#include "1-primitives/traits/is_null_pointer.hpp"
#include "1-primitives/traits/is_pointer.hpp"
#include "1-primitives/traits/is_same.hpp"
#include "1-primitives/traits/remove_const.hpp"
#include "1-primitives/traits/remove_cvref.hpp"
#include "1-primitives/traits/remove_pointer.hpp"
#include "1-primitives/traits/underlying_type.hpp"
#include "1-primitives/utility/declval.hpp"
#include "2-vocabulary/error.hpp"
#include "2-vocabulary/sv/basic.hpp"
#include "5-collections/format/text_writer.hpp"


// -- CA NAMESPACE -----------------------------------------------------------

namespace ca {


	// -- DISPLAY KIND -------------------------------------------------------

	namespace impl {

		enum class display_kind : ca::u8 {
			none,
			boolean,
			character,
			code_point,
			signed_integer,
			unsigned_integer,
			floating,
			string,
			wide_string,
			pointer,
			error,
			custom,
		};

		template <typename T>
		concept has_display = requires (const T& value, ca::text_writer& out) {
			value.display(out);
		};

		template <typename T>
		inline constexpr bool is_text_view = false;

		template <typename C>
		inline constexpr bool is_text_view<ca::sv::basic<C>> = true;

		template <typename V>
		concept is_view_of_text = ca::impl::is_text_view<ca::remove_cvref<V>>;

		template <typename T>
		concept has_view = requires (const T& value) {
			{ value.view() } -> ca::impl::is_view_of_text;
		};

		/* @DOC The code unit type of a text argument: a C pointer or array, a
		   sv::basic<C>, or a type whose view() returns one. */
		template <typename T>
		[[nodiscard]] consteval auto text_unit_size(void) noexcept -> ca::usz {

			if constexpr (ca::is_pointer<ca::decay<T>>)
				return sizeof(ca::remove_pointer<ca::decay<T>>);
			else if constexpr (ca::impl::is_text_view<T>)
				return sizeof(typename T::value_type);
			else
				return sizeof(typename ca::remove_cvref<decltype(ca::declval<const T&>().view())>::value_type);
		}

		template <typename T>
		inline constexpr bool is_character_pointer = ca::impl::is_any_of<ca::remove_const<ca::remove_pointer<ca::decay<T>>>, char, char8_t, char16_t, char32_t, wchar_t>
			&& ca::is_pointer<ca::decay<T>>;

		template <typename T>
		[[nodiscard]] consteval auto classify(void) noexcept -> ca::impl::display_kind {

			if constexpr (ca::impl::has_display<T>)
				return ca::impl::display_kind::custom;
			else if constexpr (ca::is_same<T, bool>)
				return ca::impl::display_kind::boolean;
			else if constexpr (ca::impl::is_any_of<T, char, char8_t>)
				return ca::impl::display_kind::character;
			else if constexpr (ca::impl::is_any_of<T, char16_t, char32_t, wchar_t>)
				return ca::impl::display_kind::code_point;
			else if constexpr (ca::impl::is_any_of<T, signed char, short, int, long, long long>)
				return ca::impl::display_kind::signed_integer;
			else if constexpr (ca::impl::is_any_of<T, unsigned char, unsigned short, unsigned int, unsigned long, unsigned long long>)
				return ca::impl::display_kind::unsigned_integer;
			else if constexpr (ca::impl::is_any_of<T, float, double> && ca_target_feature(floats))
				return ca::impl::display_kind::floating;
			else if constexpr (ca::is_enum<T>)
				return ca::impl::classify<ca::underlying_type<T>>();
			else if constexpr (ca::impl::is_character_pointer<T> || ca::impl::is_text_view<T> || ca::impl::has_view<T>)
				return ca::impl::text_unit_size<T>() == 1U ? ca::impl::display_kind::string : ca::impl::display_kind::wide_string;
			else if constexpr (ca::is_same<T, ca::error>)
				return ca::impl::display_kind::error;
			else if constexpr (ca::is_null_pointer<T>)
				return ca::impl::display_kind::pointer;
			else if constexpr (ca::is_pointer<T> && !ca::is_function<ca::remove_pointer<T>>)
				return ca::impl::display_kind::pointer;
			else
				return ca::impl::display_kind::none;
		}

		template <typename T>
		inline constexpr ca::impl::display_kind display_kind_of = ca::impl::classify<ca::remove_cvref<T>>();

	} // namespace impl

} // namespace ca

#endif // ca_collections_format_impl_display_kind_hpp
