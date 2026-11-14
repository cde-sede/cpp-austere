#include "1-primitives/config/architecture.hpp"
#include "1-primitives/config/platform.hpp"
#include "2-vocabulary/failure.hpp"
#include "3-system/error/last_error.hpp"
#include "3-system/net/impl/sockaddr_ipv4.hpp"
#include "3-system/net/peer_endpoint.hpp"
#include "3-system/syscall/syscalls.hpp"

#include <sys/socket.h>


[[nodiscard]] auto ca::sys::peer_endpoint(const int fd) noexcept -> ca::result<ca::sys::ipv4_endpoint> {

	ca::sys::impl::sockaddr_ipv4 address{};
	socklen_t length = sizeof(address);
#if ca_platform(linux) && ca_architecture(x86_64)
	const auto done = ca::syscall::getpeername(fd, &address, &length);
	if (!done) [[unlikely]]
		return done.propagate();
#else
	if (::getpeername(fd, reinterpret_cast<struct sockaddr*>(&address), &length) == -1) [[unlikely]]
		return ca::failure{ca::sys::impl::libc_error()};
#endif
	return ca::sys::impl::checked_endpoint(address, length);
}
