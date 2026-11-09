#include "1-primitives/config/architecture.hpp"
#include "1-primitives/config/platform.hpp"
#include "2-vocabulary/failure.hpp"
#include "3-system/error/errc.hpp"
#include "3-system/error/last_error.hpp"
#include "3-system/fs/options.hpp"
#include "3-system/io/impl/apply_open_flags.hpp"
#include "3-system/net/impl/sockaddr_unix.hpp"
#include "3-system/net/unix_connect.hpp"
#include "3-system/syscall/syscalls.hpp"

#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>


namespace {

	/* fd closed after a failure that is returned instead. */
	auto discard(const int fd) noexcept -> void {
#if ca_platform(linux) && ca_architecture(x86_64)
		(void)ca::syscall::close(fd);
#else
		(void)::close(fd);
#endif
	}

} // namespace

[[nodiscard]] auto ca::sys::unix_connect(const ca::sv::str path, const int flags) noexcept -> ca::result<int> {

	if ((flags & ~(ca::fs::impl::CLOEXEC | ca::fs::impl::NONBLOCK)) != 0) [[unlikely]]
		return ca::failure{ca::errc::invalid_argument};
	if (path.size() == 0U || path.size() >= ca::sys::impl::unix_path_capacity) [[unlikely]]
		return ca::failure{ca::errc::name_too_long};
	ca::sys::impl::sockaddr_unix address{};
#if !ca_platform(linux)
	address.length = static_cast<ca::u8>(sizeof(address));
#endif
	address.family = static_cast<decltype(address.family)>(ca::sys::impl::address_family_unix);
	for (ca::usz i = 0U; i < path.size(); ++i)
		address.path[i] = path.data()[i];
	// The length counts the path without its zero: an abstract name ('\0' first) is exactly its
	// bytes, and a filesystem path is read up to the zero either way.
	const ca::usz length = __builtin_offsetof(ca::sys::impl::sockaddr_unix, path) + path.size();

#if ca_platform(linux) && ca_architecture(x86_64)
	const auto made = ca::syscall::socket(ca::sys::impl::address_family_unix, ca::sys::impl::socket_stream | flags, 0);
	if (!made) [[unlikely]]
		return made.propagate();
	const int fd = static_cast<int>(*made);
	for (;;) {
		const auto done = ca::syscall::connect(fd, &address, length);
		if (done)
			return fd;
		if (done.error() == ca::errc::interrupted)
			continue;
		discard(fd);
		return done.propagate();
	}
#else
#	if ca_platform(linux)
	const int fd = ::socket(AF_UNIX, SOCK_STREAM | flags, 0);
	if (fd == -1) [[unlikely]]
		return ca::failure{ca::sys::impl::libc_error()};
#	else
	const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd == -1) [[unlikely]]
		return ca::failure{ca::sys::impl::libc_error()};
	const auto applied = ca::sys::impl::apply_open_flags(fd, flags);
	if (!applied) [[unlikely]] {
		discard(fd);
		return applied.propagate();
	}
#	endif
	for (;;) {
		if (::connect(fd, reinterpret_cast<const struct sockaddr*>(&address), static_cast<socklen_t>(length)) == 0)
			return fd;
		const ca::error error = ca::sys::impl::libc_error();
		if (error == ca::errc::interrupted)
			continue;
		discard(fd);
		return ca::failure{error};
	}
#endif
}
