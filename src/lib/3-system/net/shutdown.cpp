#include "1-primitives/config/architecture.hpp"
#include "1-primitives/config/platform.hpp"
#include "2-vocabulary/failure.hpp"
#include "3-system/error/last_error.hpp"
#include "3-system/net/impl/socket_constants.hpp"
#include "3-system/net/shutdown.hpp"
#include "3-system/syscall/syscalls.hpp"

#include <sys/socket.h>


[[nodiscard]] auto ca::sys::shutdown(const int fd, const ca::sys::shutdown_side side) noexcept -> ca::result<void> {

	const int how = side == ca::sys::shutdown_side::read ? ca::sys::impl::shut_read
		: side == ca::sys::shutdown_side::write ? ca::sys::impl::shut_write
		: ca::sys::impl::shut_both;
#if ca_platform(linux) && ca_architecture(x86_64)
	const auto done = ca::syscall::shutdown(fd, how);
	if (!done) [[unlikely]]
		return done.propagate();
#else
	if (::shutdown(fd, how) == -1) [[unlikely]]
		return ca::failure{ca::sys::impl::libc_error()};
#endif
	return ca::result<void>{};
}
