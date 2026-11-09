#include "1-primitives/config/architecture.hpp"
#include "1-primitives/config/platform.hpp"
#include "2-vocabulary/failure.hpp"
#include "3-system/error/last_error.hpp"
#include "3-system/net/impl/sockaddr_ipv4.hpp"
#include "3-system/net/receive_from.hpp"
#include "3-system/syscall/restarting.hpp"
#include "3-system/syscall/syscalls.hpp"

#include <errno.h>
#include <sys/socket.h>
#include <sys/uio.h>


[[nodiscard]] auto ca::sys::receive_from(const int fd, const ca::span<ca::byte> buffer) noexcept -> ca::result<ca::sys::datagram> {

	ca::sys::impl::sockaddr_ipv4 address{};
	socklen_t length = sizeof(address);
#if ca_platform(linux) && ca_architecture(x86_64)
	const auto done = ca::syscall::restarting([&] {
		length = sizeof(address);
		return ca::syscall::recvfrom(fd, buffer.data(), buffer.size(), MSG_TRUNC, &address, &length);
	});
	if (!done) [[unlikely]]
		return done.propagate();
	const ca::usz whole = static_cast<ca::usz>(*done);
	const bool truncated = whole > buffer.size();
	const ca::usz size = truncated ? buffer.size() : whole;
#else
	struct iovec part{buffer.data(), buffer.size()};
	struct msghdr message{};
	message.msg_name = &address;
	message.msg_iov = &part;
	message.msg_iovlen = 1;
	ssize_t done{-1};
	do {
		message.msg_namelen = sizeof(address);
		message.msg_flags = 0;
		done = ::recvmsg(fd, &message, 0);
	} while (done == -1 && errno == EINTR);
	if (done == -1) [[unlikely]]
		return ca::failure{ca::sys::impl::libc_error()};
	length = message.msg_namelen;
	const ca::usz size = static_cast<ca::usz>(done);
	const bool truncated = (message.msg_flags & MSG_TRUNC) != 0;
#endif
	const auto from = ca::sys::impl::checked_endpoint(address, length);
	if (!from) [[unlikely]]
		return from.propagate();
	return ca::sys::datagram{size, truncated, *from};
}
