#include "1-primitives/config/architecture.hpp"
#include "1-primitives/config/platform.hpp"
#include "2-vocabulary/failure.hpp"
#include "3-system/error/errc.hpp"
#include "3-system/error/last_error.hpp"
#include "3-system/io/poll.hpp"
#include "3-system/io/poll_entry.hpp"
#include "3-system/io/poll_events.hpp"
#include "3-system/net/connect.hpp"
#include "3-system/net/impl/sockaddr_ipv4.hpp"
#include "3-system/net/socket_error.hpp"
#include "3-system/syscall/syscalls.hpp"

#include <sys/socket.h>


namespace {

	/* A blocking connect a signal interrupted goes on in the kernel; waits for its end. */
	[[nodiscard]] auto finish_interrupted(const int fd) noexcept -> ca::result<void> {

		ca::sys::poll_entry entry{fd, ca::sys::poll_events::writable};
		for (;;) {
			const auto ready = ca::sys::poll(ca::span<ca::sys::poll_entry>{&entry, 1U}, -1);
			if (ready)
				return ca::sys::socket_error(fd);
			if (ready.error() != ca::errc::interrupted) [[unlikely]]
				return ready.propagate();
		}
	}

} // namespace

[[nodiscard]] auto ca::sys::connect(const int fd, const ca::sys::ipv4_endpoint endpoint) noexcept -> ca::result<void> {

	const ca::sys::impl::sockaddr_ipv4 address = ca::sys::impl::to_sockaddr(endpoint);
#if ca_platform(linux) && ca_architecture(x86_64)
	const auto done = ca::syscall::connect(fd, &address, sizeof(address));
	if (done)
		return ca::result<void>{};
	const ca::error error = done.error();
#else
	if (::connect(fd, reinterpret_cast<const struct sockaddr*>(&address), sizeof(address)) == 0)
		return ca::result<void>{};
	const ca::error error = ca::sys::impl::libc_error();
#endif
	if (error == ca::errc::interrupted)
		return finish_interrupted(fd);
	return ca::failure{error};
}
