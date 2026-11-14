#include "1-primitives/config/architecture.hpp"
#include "1-primitives/config/platform.hpp"
#include "2-vocabulary/failure.hpp"
#include "3-system/error/last_error.hpp"
#include "3-system/net/impl/socket_constants.hpp"
#include "3-system/net/set_socket_option.hpp"
#include "3-system/syscall/syscalls.hpp"

#include <sys/socket.h>


[[nodiscard]] auto ca::sys::set_socket_option(const int fd, const ca::sys::socket_option option, const bool on) noexcept -> ca::result<void> {

	int level{ca::sys::impl::socket_level};
	int name{0};
	switch (option) {
		case ca::sys::socket_option::reuse_address: name = ca::sys::impl::socket_reuse_address; break;
		case ca::sys::socket_option::reuse_port:    name = ca::sys::impl::socket_reuse_port; break;
		case ca::sys::socket_option::keep_alive:    name = ca::sys::impl::socket_keep_alive; break;
		case ca::sys::socket_option::no_delay:
			level = ca::sys::impl::protocol_tcp;
			name = ca::sys::impl::tcp_no_delay;
			break;
	}
	const int value = on ? 1 : 0;
#if ca_platform(linux) && ca_architecture(x86_64)
	const auto done = ca::syscall::setsockopt(fd, level, name, &value, sizeof(value));
	if (!done) [[unlikely]]
		return done.propagate();
#else
	if (::setsockopt(fd, level, name, &value, sizeof(value)) == -1) [[unlikely]]
		return ca::failure{ca::sys::impl::libc_error()};
#endif
	return ca::result<void>{};
}
