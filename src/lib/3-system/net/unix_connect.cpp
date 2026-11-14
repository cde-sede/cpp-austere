#include "1-primitives/config/architecture.hpp"
#include "1-primitives/config/platform.hpp"
#include "2-vocabulary/failure.hpp"
#include "3-system/error/errc.hpp"
#include "3-system/error/last_error.hpp"
#include "3-system/net/impl/open_socket.hpp"
#include "3-system/net/impl/sockaddr_unix.hpp"
#include "3-system/net/unix_connect.hpp"
#include "3-system/syscall/syscalls.hpp"

#include <sys/socket.h>


[[nodiscard]] auto ca::sys::unix_connect(const ca::sv::str path, const int flags) noexcept -> ca::result<int> {

	ca::sys::impl::sockaddr_unix address{};
	const auto length = ca::sys::impl::to_sockaddr(path, address);
	if (!length) [[unlikely]]
		return length.propagate();
	const auto made = ca::sys::impl::open_socket(ca::sys::impl::address_family_unix, ca::sys::impl::socket_stream, 0, flags);
	if (!made) [[unlikely]]
		return made.propagate();
	const int fd = *made;
	for (;;) {
#if ca_platform(linux) && ca_architecture(x86_64)
		const auto done = ca::syscall::connect(fd, &address, *length);
		if (done)
			return fd;
		const ca::error error = done.error();
#else
		if (::connect(fd, reinterpret_cast<const struct sockaddr*>(&address), static_cast<socklen_t>(*length)) == 0)
			return fd;
		const ca::error error = ca::sys::impl::libc_error();
#endif
		if (error == ca::errc::interrupted)
			continue;
		ca::sys::impl::discard_socket(fd);
		return ca::failure{error};
	}
}
