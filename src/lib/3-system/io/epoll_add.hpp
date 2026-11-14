#ifndef ca_system_io_epoll_add_hpp
#define ca_system_io_epoll_add_hpp

#include "1-primitives/integers/u32.hpp"
#include "1-primitives/integers/u64.hpp"
#include "2-vocabulary/result.hpp"


// -- CA NAMESPACE -----------------------------------------------------------

namespace ca {


	// -- SYS ----------------------------------------------------------------

	namespace sys {


		// -- EPOLL ADD ------------------------------------------------------

		/* @DOC Starts epoll descriptor epfd watching fd for events (sys::epoll_events), each report
		   carrying data. errc::exists when epfd already watches fd; errc::not_permitted for a
		   descriptor epoll cannot watch (a regular file, a directory). */
		[[nodiscard]] auto epoll_add(int epfd, int fd, ca::u32 events, ca::u64 data) noexcept -> ca::result<void>;

	} // namespace sys

} // namespace ca

#endif // ca_system_io_epoll_add_hpp
