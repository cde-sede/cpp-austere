#include "1-primitives/config/platform.hpp"

#if !ca_platform(linux)

#include "2-vocabulary/failure.hpp"
#include "3-system/error/last_error.hpp"
#include "3-system/io/impl/apply_open_flags.hpp"

#include <fcntl.h>


[[nodiscard]] auto ca::sys::impl::apply_open_flags(const int fd, const int flags) noexcept -> ca::result<void> {

	if ((flags & O_CLOEXEC) != 0 && ::fcntl(fd, F_SETFD, FD_CLOEXEC) == -1) [[unlikely]]
		return ca::failure{ca::sys::impl::libc_error()};
	if ((flags & O_NONBLOCK) != 0) {
		const int status = ::fcntl(fd, F_GETFL);
		if (status == -1 || ::fcntl(fd, F_SETFL, status | O_NONBLOCK) == -1) [[unlikely]]
			return ca::failure{ca::sys::impl::libc_error()};
	}
	return ca::result<void>{};
}

#endif
