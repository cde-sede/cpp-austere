#ifndef ca_system_io_epoll_remove_hpp
#define ca_system_io_epoll_remove_hpp

#include "2-vocabulary/result.hpp"


// -- CA NAMESPACE -----------------------------------------------------------

namespace ca {


	// -- SYS ----------------------------------------------------------------

	namespace sys {


		// -- EPOLL REMOVE ---------------------------------------------------

		/* @DOC Stops epfd watching fd. Closing fd removes it too, once no other descriptor (dup,
		   fork) refers to the same open file. errc::no_entry when epfd does not watch fd. */
		[[nodiscard]] auto epoll_remove(int epfd, int fd) noexcept -> ca::result<void>;

	} // namespace sys

} // namespace ca

#endif // ca_system_io_epoll_remove_hpp
