#include "1-primitives/config/architecture.hpp"
#include "1-primitives/config/platform.hpp"
#include "2-vocabulary/failure.hpp"
#include "3-system/error/errc.hpp"
#include "3-system/error/last_error.hpp"
#include "3-system/fs/options.hpp"
#include "3-system/io/impl/apply_open_flags.hpp"
#include "3-system/net/accept.hpp"
#include "3-system/net/impl/open_socket.hpp"
#include "3-system/syscall/restarting.hpp"
#include "3-system/syscall/syscalls.hpp"

#include <errno.h>
#include <sys/socket.h>


[[nodiscard]] auto ca::sys::accept(const int fd, const int flags) noexcept -> ca::result<int> {

	if ((flags & ~(ca::fs::impl::CLOEXEC | ca::fs::impl::NONBLOCK)) != 0) [[unlikely]]
		return ca::failure{ca::errc::invalid_argument};
#if ca_platform(linux) && ca_architecture(x86_64)
	const auto taken = ca::syscall::restarting([&] {
		return ca::syscall::accept4(fd, 0, 0, flags);
	});
	if (!taken) [[unlikely]]
		return taken.propagate();
	return static_cast<int>(*taken);
#else
	int peer{-1};
	do
#	if ca_platform(linux)
		peer = ::accept4(fd, nullptr, nullptr, flags);
#	else
		peer = ::accept(fd, nullptr, nullptr);
#	endif
	while (peer == -1 && errno == EINTR);
	if (peer == -1) [[unlikely]]
		return ca::failure{ca::sys::impl::libc_error()};
#	if !ca_platform(linux)
	const auto applied = ca::sys::impl::apply_open_flags(peer, flags);
	if (!applied) [[unlikely]] {
		ca::sys::impl::discard_socket(peer);
		return applied.propagate();
	}
#	endif
	return peer;
#endif
}
