#include "1-primitives/config/architecture.hpp"
#include "1-primitives/config/platform.hpp"
#include "2-vocabulary/failure.hpp"
#include "3-system/error/last_error.hpp"
#include "3-system/net/impl/sockaddr_ipv4.hpp"
#include "3-system/net/send_to.hpp"
#include "3-system/syscall/restarting.hpp"
#include "3-system/syscall/syscalls.hpp"

#include <errno.h>
#include <sys/socket.h>


[[nodiscard]] auto ca::sys::send_to(const int fd, const ca::span<const ca::byte> bytes, const ca::sys::ipv4_endpoint endpoint) noexcept -> ca::result<ca::usz> {

	const ca::sys::impl::sockaddr_ipv4 address = ca::sys::impl::to_sockaddr(endpoint);
#if ca_platform(linux) && ca_architecture(x86_64)
	return ca::syscall::restarting([&] {
		return ca::syscall::sendto(fd, bytes.data(), bytes.size(), 0, &address, sizeof(address));
	}).transform([](const ca::isz done) {
		return static_cast<ca::usz>(done);
	});
#else
	ssize_t done{-1};
	do
		done = ::sendto(fd, bytes.data(), bytes.size(), 0, reinterpret_cast<const struct sockaddr*>(&address), sizeof(address));
	while (done == -1 && errno == EINTR);
	if (done == -1) [[unlikely]]
		return ca::failure{ca::sys::impl::libc_error()};
	return static_cast<ca::usz>(done);
#endif
}
