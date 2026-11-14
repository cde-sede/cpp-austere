#include "3-system/io/epoll_remove.hpp"
#include "3-system/io/impl/epoll_control.hpp"


[[nodiscard]] auto ca::sys::epoll_remove(const int epfd, const int fd) noexcept -> ca::result<void> {
	return ca::sys::impl::epoll_control(epfd, ca::sys::impl::epoll_op_remove, fd, nullptr);
}
