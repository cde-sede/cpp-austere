#include "1-primitives/config/architecture.hpp"
#include "1-primitives/config/platform.hpp"
#include "2-vocabulary/failure.hpp"
#include "3-system/error/errc.hpp"
#include "3-system/error/last_error.hpp"
#include "3-system/fs/options.hpp"
#include "3-system/io/impl/apply_open_flags.hpp"
#include "3-system/net/impl/sockaddr_ipv4.hpp"
#include "3-system/net/udp_socket.hpp"
#include "3-system/syscall/syscalls.hpp"

#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#if ca_platform(linux)
static_assert(SOCK_NONBLOCK == O_NONBLOCK && SOCK_CLOEXEC == O_CLOEXEC);
#endif


[[nodiscard]] auto ca::sys::udp_socket(const int flags) noexcept -> ca::result<int> {

	if ((flags & ~(ca::fs::impl::CLOEXEC | ca::fs::impl::NONBLOCK)) != 0) [[unlikely]]
		return ca::failure{ca::errc::invalid_argument};
#if ca_platform(linux) && ca_architecture(x86_64)
	const auto made = ca::syscall::socket(ca::sys::impl::address_family_ipv4, ca::sys::impl::socket_datagram | flags, ca::sys::impl::protocol_udp);
	if (!made) [[unlikely]]
		return made.propagate();
	return static_cast<int>(*made);
#elif ca_platform(linux)
	const int fd = ::socket(AF_INET, SOCK_DGRAM | flags, IPPROTO_UDP);
	if (fd == -1) [[unlikely]]
		return ca::failure{ca::sys::impl::libc_error()};
	return fd;
#else
	const int fd = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (fd == -1) [[unlikely]]
		return ca::failure{ca::sys::impl::libc_error()};
	const auto applied = ca::sys::impl::apply_open_flags(fd, flags);
	if (!applied) [[unlikely]] {
		::close(fd);
		return applied.propagate();
	}
	return fd;
#endif
}
