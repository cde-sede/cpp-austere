#ifndef ca_system_io_epoll_modify_hpp
#define ca_system_io_epoll_modify_hpp

#include "1-primitives/integers/u32.hpp"
#include "1-primitives/integers/u64.hpp"
#include "2-vocabulary/result.hpp"


// -- CA NAMESPACE -----------------------------------------------------------

namespace ca {


	// -- SYS ----------------------------------------------------------------

	namespace sys {


		// -- EPOLL MODIFY ---------------------------------------------------

		/* @DOC Replaces the events and data epfd watches fd for, rearming a oneshot registration.
		   errc::no_entry when epfd does not watch fd. */
		[[nodiscard]] auto epoll_modify(int epfd, int fd, ca::u32 events, ca::u64 data) noexcept -> ca::result<void>;

	} // namespace sys

} // namespace ca

#endif // ca_system_io_epoll_modify_hpp
