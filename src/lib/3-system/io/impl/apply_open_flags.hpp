#ifndef ca_system_io_impl_apply_open_flags_hpp
#define ca_system_io_impl_apply_open_flags_hpp

#include "1-primitives/config/platform.hpp"
#include "2-vocabulary/result.hpp"


// -- CA NAMESPACE -----------------------------------------------------------

namespace ca {


	// -- SYS ----------------------------------------------------------------

	namespace sys {


		// -- APPLY OPEN FLAGS -----------------------------------------------

		namespace impl {

#if !ca_platform(linux)
			/* @DOC Sets fs::cloexec and fs::nonblock (their OPEN_VALUE in flags) on fd with fcntl,
			   for systems that cannot set them when fd is made. The caller closes fd on failure. */
			[[nodiscard]] auto apply_open_flags(int fd, int flags) noexcept -> ca::result<void>;
#endif

		} // namespace impl

	} // namespace sys

} // namespace ca

#endif // ca_system_io_impl_apply_open_flags_hpp
