#include "1-primitives/config/architecture.hpp"
#include "1-primitives/config/platform.hpp"
#include "2-vocabulary/failure.hpp"
#include "3-system/error/last_error.hpp"
#include "3-system/net/impl/open_socket.hpp"
#include "3-system/net/impl/sockaddr_unix.hpp"
#include "3-system/net/listen.hpp"
#include "3-system/net/unix_listen.hpp"
#include "3-system/syscall/syscalls.hpp"

#include <sys/socket.h>


[[nodiscard]] auto ca::sys::unix_listen(const ca::sv::str path, const int backlog, const int flags) noexcept -> ca::result<int> {

	ca::sys::impl::sockaddr_unix address{};
	const auto length = ca::sys::impl::to_sockaddr(path, address);
	if (!length) [[unlikely]]
		return length.propagate();
	const auto made = ca::sys::impl::open_socket(ca::sys::impl::address_family_unix, ca::sys::impl::socket_stream, 0, flags);
	if (!made) [[unlikely]]
		return made.propagate();
	const int fd = *made;
#if ca_platform(linux) && ca_architecture(x86_64)
	ca::result<void> done{};
	const auto bound = ca::syscall::bind(fd, &address, *length);
	if (!bound) [[unlikely]]
		done = bound.propagate();
#else
	ca::result<void> done{};
	if (::bind(fd, reinterpret_cast<const struct sockaddr*>(&address), static_cast<socklen_t>(*length)) == -1) [[unlikely]]
		done = ca::failure{ca::sys::impl::libc_error()};
#endif
	if (done)
		done = ca::sys::listen(fd, backlog);
	if (!done) [[unlikely]] {
		ca::sys::impl::discard_socket(fd);
		return done.propagate();
	}
	return fd;
}
