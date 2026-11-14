#include "1-primitives/config/architecture.hpp"
#include "1-primitives/config/platform.hpp"
#include "2-vocabulary/failure.hpp"
#include "3-system/error/last_error.hpp"
#include "3-system/net/listen.hpp"
#include "3-system/syscall/syscalls.hpp"

#include <sys/socket.h>


[[nodiscard]] auto ca::sys::listen(const int fd, const int backlog) noexcept -> ca::result<void> {

	const int queued = backlog < 0 ? SOMAXCONN : backlog;
#if ca_platform(linux) && ca_architecture(x86_64)
	const auto done = ca::syscall::listen(fd, queued);
	if (!done) [[unlikely]]
		return done.propagate();
#else
	if (::listen(fd, queued) == -1) [[unlikely]]
		return ca::failure{ca::sys::impl::libc_error()};
#endif
	return ca::result<void>{};
}
