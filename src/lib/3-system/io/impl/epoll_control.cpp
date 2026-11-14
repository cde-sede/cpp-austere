#include "1-primitives/config/architecture.hpp"
#include "1-primitives/config/platform.hpp"
#include "2-vocabulary/failure.hpp"
#include "3-system/error/errc.hpp"
#include "3-system/error/last_error.hpp"
#include "3-system/io/epoll_events.hpp"
#include "3-system/io/impl/epoll_control.hpp"
#include "3-system/syscall/syscalls.hpp"

#if ca_platform(linux)
#	include <sys/epoll.h>

namespace ca {


	namespace sys {


		namespace impl {

			struct epoll_layout final {
					static_assert(sizeof(ca::sys::epoll_event) == sizeof(::epoll_event) && alignof(ca::sys::epoll_event) == alignof(::epoll_event));
					static_assert(__builtin_offsetof(ca::sys::epoll_event, _events) == __builtin_offsetof(::epoll_event, events));
					static_assert(__builtin_offsetof(ca::sys::epoll_event, _data) == __builtin_offsetof(::epoll_event, data));
					static_assert(sizeof(ca::sys::epoll_event::_data) == sizeof(::epoll_data_t));
			}; // struct epoll_layout

		} // namespace impl

	} // namespace sys

} // namespace ca

static_assert(ca::sys::epoll_events::readable == EPOLLIN);
static_assert(ca::sys::epoll_events::urgent == EPOLLPRI);
static_assert(ca::sys::epoll_events::writable == EPOLLOUT);
static_assert(ca::sys::epoll_events::error == EPOLLERR);
static_assert(ca::sys::epoll_events::hangup == EPOLLHUP);
static_assert(ca::sys::epoll_events::peer_closed == EPOLLRDHUP);
static_assert(ca::sys::epoll_events::exclusive == EPOLLEXCLUSIVE);
static_assert(ca::sys::epoll_events::wakeup == EPOLLWAKEUP);
static_assert(ca::sys::epoll_events::oneshot == EPOLLONESHOT);
static_assert(ca::sys::epoll_events::edge == static_cast<ca::u32>(EPOLLET));
static_assert(ca::sys::impl::epoll_op_add == EPOLL_CTL_ADD);
static_assert(ca::sys::impl::epoll_op_remove == EPOLL_CTL_DEL);
static_assert(ca::sys::impl::epoll_op_modify == EPOLL_CTL_MOD);
#endif


[[nodiscard]] auto ca::sys::impl::epoll_control(const int epfd, const int op, const int fd, ca::sys::epoll_event* const event) noexcept -> ca::result<void> {

#if ca_platform(linux) && ca_architecture(x86_64)
	const auto done = ca::syscall::epoll_ctl(epfd, op, fd, event);
	if (!done) [[unlikely]]
		return done.propagate();
	return ca::result<void>{};
#elif ca_platform(linux)
	if (::epoll_ctl(epfd, op, fd, reinterpret_cast<::epoll_event*>(event)) == -1) [[unlikely]]
		return ca::failure{ca::sys::impl::libc_error()};
	return ca::result<void>{};
#else
	static_cast<void>(epfd);
	static_cast<void>(op);
	static_cast<void>(fd);
	static_cast<void>(event);
	return ca::failure{ca::errc::not_supported};
#endif
}
