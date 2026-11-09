#ifndef ca_system_error_errc_hpp
#define ca_system_error_errc_hpp

#include "1-primitives/config/platform.hpp"
#include "2-vocabulary/error.hpp"


// -- CA NAMESPACE -----------------------------------------------------------

namespace ca {


	// -- ERRC ---------------------------------------------------------------

	/* @DOC errno codes as constant errors, without <errno.h>. Values are checked against it when
	   the library builds. */
	namespace errc {

		inline constexpr ca::error not_permitted{1};
		inline constexpr ca::error no_entry{2};
		inline constexpr ca::error no_process{3};
		inline constexpr ca::error interrupted{4};
		inline constexpr ca::error io_error{5};
		inline constexpr ca::error bad_descriptor{9};
		inline constexpr ca::error no_child{10};
		inline constexpr ca::error out_of_memory{12};
		inline constexpr ca::error bad_address{14};
		inline constexpr ca::error access_denied{13};
		inline constexpr ca::error busy{16};
		inline constexpr ca::error exists{17};
		inline constexpr ca::error cross_device{18};
		inline constexpr ca::error not_a_directory{20};
		inline constexpr ca::error is_a_directory{21};
		inline constexpr ca::error invalid_argument{22};
		inline constexpr ca::error too_many_files{24};
		inline constexpr ca::error not_a_terminal{25};
		inline constexpr ca::error no_space{28};
		inline constexpr ca::error not_seekable{29};
		inline constexpr ca::error broken_pipe{32};
		inline constexpr ca::error out_of_domain{33};
		inline constexpr ca::error out_of_range{34};

#if ca_platform(linux)
		inline constexpr ca::error try_again{11};
		inline constexpr ca::error name_too_long{36};
		inline constexpr ca::error not_empty{39};
		inline constexpr ca::error symlink_loop{40};
		inline constexpr ca::error not_supported{95};
		inline constexpr ca::error address_family_not_supported{97};
		inline constexpr ca::error connection_refused{111};
		inline constexpr ca::error not_implemented{38};
		inline constexpr ca::error timed_out{110};
		inline constexpr ca::error not_recoverable{131};
#else
		inline constexpr ca::error try_again{35};
		inline constexpr ca::error name_too_long{63};
		inline constexpr ca::error not_empty{66};
		inline constexpr ca::error symlink_loop{62};
		inline constexpr ca::error not_supported{45};
		inline constexpr ca::error address_family_not_supported{47};
		inline constexpr ca::error connection_refused{61};
		inline constexpr ca::error not_implemented{78};
		inline constexpr ca::error timed_out{60};
		inline constexpr ca::error not_recoverable{104};
#endif

	} // namespace errc

} // namespace ca

#endif // ca_system_error_errc_hpp
