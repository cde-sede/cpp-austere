#include "1-primitives/config/architecture.hpp"
#include "1-primitives/config/platform.hpp"
#include "2-vocabulary/failure.hpp"
#include "3-system/error/last_error.hpp"
#include "3-system/net/bind.hpp"
#include "3-system/net/impl/sockaddr_ipv4.hpp"
#include "3-system/syscall/syscalls.hpp"

#include <sys/socket.h>


[[nodiscard]] auto ca::sys::bind(const int fd, const ca::sys::ipv4_endpoint endpoint) noexcept -> ca::result<void> {

	const ca::sys::impl::sockaddr_ipv4 address = ca::sys::impl::to_sockaddr(endpoint);
#if ca_platform(linux) && ca_architecture(x86_64)
	const auto done = ca::syscall::bind(fd, &address, sizeof(address));
	if (!done) [[unlikely]]
		return done.propagate();
#else
	if (::bind(fd, reinterpret_cast<const struct sockaddr*>(&address), sizeof(address)) == -1) [[unlikely]]
		return ca::failure{ca::sys::impl::libc_error()};
#endif
	return ca::result<void>{};
}
