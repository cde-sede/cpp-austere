#include "1-primitives/atomic/atomic.hpp"
#include "1-primitives/atomic/memory_order.hpp"
#include "1-primitives/config/architecture.hpp"
#include "1-primitives/config/platform.hpp"
#include "1-primitives/integers/i32.hpp"
#include "2-vocabulary/failure.hpp"
#include "3-system/error/errc.hpp"
#include "3-system/error/last_error.hpp"
#include "3-system/io/epoll_wait.hpp"
#include "3-system/syscall/syscalls.hpp"

#if ca_platform(linux)
#	include <sys/epoll.h>
#	include <time.h>
#endif


#if ca_platform(linux)
namespace {

	/* timeout_ns as epoll_wait milliseconds: rounded up, so a wait never ends early; capped. */
	[[nodiscard]] constexpr auto to_milliseconds(const ca::i64 timeout_ns) noexcept -> int {

		if (timeout_ns < 0)
			return -1;
		const ca::i64 ms = timeout_ns / 1000000 + (timeout_ns % 1000000 != 0 ? 1 : 0);
		return ms > 0x7FFFFFFF ? 0x7FFFFFFF : static_cast<int>(ms);
	}

	/* reported's size as epoll's maxevents, capped as the kernel caps it. */
	[[nodiscard]] constexpr auto to_count(const ca::usz size) noexcept -> int {
		constexpr ca::usz most = 0x7FFFFFFFU / sizeof(ca::sys::epoll_event);
		return static_cast<int>(size > most ? most : size);
	}

} // namespace
#endif

[[nodiscard]] auto ca::sys::epoll_wait(const int epfd, const ca::span<ca::sys::epoll_event> reported, const ca::i64 timeout_ns) noexcept -> ca::result<ca::usz> {

	if (reported.size() == 0U) [[unlikely]]
		return ca::failure{ca::errc::invalid_argument};
#if ca_platform(linux) && ca_architecture(x86_64)
	constinit static ca::atomic<bool> without_pwait2{false};
	if (timeout_ns > 0 && !without_pwait2.load<ca::memory_order::relaxed>()) {
		const timespec timeout{static_cast<time_t>(timeout_ns / 1000000000), static_cast<long>(timeout_ns % 1000000000)};
		const auto ready = ca::syscall::epoll_pwait2(epfd, reported.data(), to_count(reported.size()), &timeout, 0, 0);
		if (ready || ready.error() != ca::errc::not_implemented)
			return ready.transform([](const ca::isz count) {
				return static_cast<ca::usz>(count);
			});
		without_pwait2.store<ca::memory_order::relaxed>(true);
	}
	return ca::syscall::epoll_wait(epfd, reported.data(), to_count(reported.size()), to_milliseconds(timeout_ns)).transform([](const ca::isz count) {
		return static_cast<ca::usz>(count);
	});
#elif ca_platform(linux)
	const int ready = ::epoll_wait(epfd, reinterpret_cast<::epoll_event*>(reported.data()), to_count(reported.size()), to_milliseconds(timeout_ns));
	if (ready == -1) [[unlikely]]
		return ca::failure{ca::sys::impl::libc_error()};
	return static_cast<ca::usz>(ready);
#else
	static_cast<void>(epfd);
	static_cast<void>(timeout_ns);
	return ca::failure{ca::errc::not_supported};
#endif
}
