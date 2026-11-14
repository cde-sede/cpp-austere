#ifndef ca_system_io_impl_epoll_control_hpp
#define ca_system_io_impl_epoll_control_hpp

#include "2-vocabulary/result.hpp"
#include "3-system/io/epoll_event.hpp"


// -- CA NAMESPACE -----------------------------------------------------------

namespace ca {


	// -- SYS ----------------------------------------------------------------

	namespace sys {


		// -- EPOLL CONTROL --------------------------------------------------

		namespace impl {

			/* @DOC EPOLL_CTL_ADD, EPOLL_CTL_DEL, EPOLL_CTL_MOD; checked against <sys/epoll.h>. */
			inline constexpr int epoll_op_add    = 1;
			inline constexpr int epoll_op_remove = 2;
			inline constexpr int epoll_op_modify = 3;

			/* @DOC epoll_ctl(epfd, op, fd, event); event null for remove. */
			[[nodiscard]] auto epoll_control(int epfd, int op, int fd, ca::sys::epoll_event* event) noexcept -> ca::result<void>;

		} // namespace impl

	} // namespace sys

} // namespace ca

#endif // ca_system_io_impl_epoll_control_hpp
