#include "3-system/net/bind.hpp"
#include "3-system/net/impl/open_socket.hpp"
#include "3-system/net/listen.hpp"
#include "3-system/net/set_socket_option.hpp"
#include "3-system/net/tcp_listen.hpp"
#include "3-system/net/tcp_socket.hpp"


[[nodiscard]] auto ca::sys::tcp_listen(const ca::sys::ipv4_endpoint endpoint, const int backlog, const int flags) noexcept -> ca::result<int> {

	const auto made = ca::sys::tcp_socket(flags);
	if (!made) [[unlikely]]
		return made.propagate();
	const int fd = *made;
	ca::result<void> done = ca::sys::set_socket_option(fd, ca::sys::socket_option::reuse_address, true);
	if (done)
		done = ca::sys::bind(fd, endpoint);
	if (done)
		done = ca::sys::listen(fd, backlog);
	if (!done) [[unlikely]] {
		ca::sys::impl::discard_socket(fd);
		return done.propagate();
	}
	return fd;
}
