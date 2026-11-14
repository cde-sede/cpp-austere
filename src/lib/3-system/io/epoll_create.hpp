#ifndef ca_system_io_epoll_create_hpp
#define ca_system_io_epoll_create_hpp

#include "2-vocabulary/result.hpp"


// -- CA NAMESPACE -----------------------------------------------------------

namespace ca {


	// -- SYS ----------------------------------------------------------------

	namespace sys {


		// -- EPOLL CREATE ---------------------------------------------------

		/* @DOC A new epoll descriptor (epoll_create1), watching nothing yet. flags: 0 or the
		   OPEN_VALUE of fs::cloexec; any other bit fails with errc::invalid_argument. Linux only:
		   errc::not_supported elsewhere. The caller closes it with sys::close. */
		[[nodiscard]] auto epoll_create(int flags) noexcept -> ca::result<int>;

	} // namespace sys

} // namespace ca

#endif // ca_system_io_epoll_create_hpp
