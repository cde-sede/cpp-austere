#include "3-system/io/epoll_add.hpp"
#include "3-system/io/epoll_event.hpp"
#include "3-system/io/impl/epoll_control.hpp"


[[nodiscard]] auto ca::sys::epoll_add(const int epfd, const int fd, const ca::u32 events, const ca::u64 data) noexcept -> ca::result<void> {

	ca::sys::epoll_event event{events, data};
	return ca::sys::impl::epoll_control(epfd, ca::sys::impl::epoll_op_add, fd, &event);
}
