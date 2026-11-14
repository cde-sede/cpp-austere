#include "1-primitives/config/architecture.hpp"
#include "1-primitives/config/platform.hpp"
#include "2-vocabulary/failure.hpp"
#include "3-system/error/errc.hpp"
#include "3-system/error/last_error.hpp"
#include "3-system/fs/options.hpp"
#include "3-system/io/epoll_create.hpp"
#include "3-system/syscall/syscalls.hpp"

#if ca_platform(linux)
#	include <fcntl.h>
#	include <sys/epoll.h>
static_assert(EPOLL_CLOEXEC == O_CLOEXEC);
#endif


[[nodiscard]] auto ca::sys::epoll_create(const int flags) noexcept -> ca::result<int> {

	if ((flags & ~ca::fs::impl::CLOEXEC) != 0) [[unlikely]]
		return ca::failure{ca::errc::invalid_argument};
#if ca_platform(linux) && ca_architecture(x86_64)
	const auto made = ca::syscall::epoll_create1(flags);
	if (!made) [[unlikely]]
		return made.propagate();
	return static_cast<int>(*made);
#elif ca_platform(linux)
	const int fd = ::epoll_create1(flags);
	if (fd == -1) [[unlikely]]
		return ca::failure{ca::sys::impl::libc_error()};
	return fd;
#else
	return ca::failure{ca::errc::not_supported};
#endif
}
