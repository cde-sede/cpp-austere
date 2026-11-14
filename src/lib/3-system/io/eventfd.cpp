#include "1-primitives/config/architecture.hpp"
#include "1-primitives/config/platform.hpp"
#include "2-vocabulary/failure.hpp"
#include "3-system/error/errc.hpp"
#include "3-system/error/last_error.hpp"
#include "3-system/fs/options.hpp"
#include "3-system/io/eventfd.hpp"
#include "3-system/io/eventfd_read.hpp"
#include "3-system/io/eventfd_write.hpp"
#include "3-system/syscall/restarting.hpp"
#include "3-system/syscall/syscalls.hpp"

#if ca_platform(linux)
#	include <errno.h>
#	include <fcntl.h>
#	include <sys/eventfd.h>
#	include <unistd.h>
static_assert(EFD_NONBLOCK == O_NONBLOCK && EFD_CLOEXEC == O_CLOEXEC);
#endif


[[nodiscard]] auto ca::sys::eventfd(const ca::u32 initial, const int flags) noexcept -> ca::result<int> {

	if ((flags & ~(ca::fs::impl::CLOEXEC | ca::fs::impl::NONBLOCK)) != 0) [[unlikely]]
		return ca::failure{ca::errc::invalid_argument};
#if ca_platform(linux) && ca_architecture(x86_64)
	const auto made = ca::syscall::eventfd2(initial, flags);
	if (!made) [[unlikely]]
		return made.propagate();
	return static_cast<int>(*made);
#elif ca_platform(linux)
	const int fd = ::eventfd(initial, flags);
	if (fd == -1) [[unlikely]]
		return ca::failure{ca::sys::impl::libc_error()};
	return fd;
#else
	static_cast<void>(initial);
	return ca::failure{ca::errc::not_supported};
#endif
}

[[nodiscard]] auto ca::sys::eventfd_write(const int fd, const ca::u64 value) noexcept -> ca::result<void> {

#if ca_platform(linux) && ca_architecture(x86_64)
	const auto done = ca::syscall::restarting([&] {
		return ca::syscall::write(fd, &value, sizeof(value));
	});
	if (!done) [[unlikely]]
		return done.propagate();
	return ca::result<void>{};
#elif ca_platform(linux)
	ssize_t done{-1};
	do
		done = ::write(fd, &value, sizeof(value));
	while (done == -1 && errno == EINTR);
	if (done == -1) [[unlikely]]
		return ca::failure{ca::sys::impl::libc_error()};
	return ca::result<void>{};
#else
	static_cast<void>(fd);
	static_cast<void>(value);
	return ca::failure{ca::errc::not_supported};
#endif
}

[[nodiscard]] auto ca::sys::eventfd_read(const int fd) noexcept -> ca::result<ca::u64> {

	ca::u64 value{0U};
#if ca_platform(linux) && ca_architecture(x86_64)
	const auto done = ca::syscall::restarting([&] {
		return ca::syscall::read(fd, &value, sizeof(value));
	});
	if (!done) [[unlikely]]
		return done.propagate();
	return value;
#elif ca_platform(linux)
	ssize_t done{-1};
	do
		done = ::read(fd, &value, sizeof(value));
	while (done == -1 && errno == EINTR);
	if (done == -1) [[unlikely]]
		return ca::failure{ca::sys::impl::libc_error()};
	return value;
#else
	static_cast<void>(fd);
	return ca::failure{ca::errc::not_supported};
#endif
}
