#include "1-primitives/config/architecture.hpp"
#include "1-primitives/config/platform.hpp"
#include "2-vocabulary/failure.hpp"
#include "3-system/error/last_error.hpp"
#include "3-system/net/impl/socket_constants.hpp"
#include "3-system/net/socket_error.hpp"
#include "3-system/syscall/syscalls.hpp"

#include <sys/socket.h>


[[nodiscard]] auto ca::sys::socket_error(const int fd) noexcept -> ca::result<void> {

	int pending{0};
	socklen_t length = sizeof(pending);
#if ca_platform(linux) && ca_architecture(x86_64)
	const auto done = ca::syscall::getsockopt(fd, ca::sys::impl::socket_level, ca::sys::impl::socket_error, &pending, &length);
	if (!done) [[unlikely]]
		return done.propagate();
#else
	if (::getsockopt(fd, ca::sys::impl::socket_level, ca::sys::impl::socket_error, &pending, &length) == -1) [[unlikely]]
		return ca::failure{ca::sys::impl::libc_error()};
#endif
	if (pending != 0) [[unlikely]]
		return ca::failure{ca::error{pending}};
	return ca::result<void>{};
}
