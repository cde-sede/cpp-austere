#ifndef ca_system_io_epoll_wait_hpp
#define ca_system_io_epoll_wait_hpp

#include "1-primitives/integers/i64.hpp"
#include "1-primitives/integers/usz.hpp"
#include "2-vocabulary/result.hpp"
#include "2-vocabulary/span.hpp"
#include "3-system/io/epoll_event.hpp"


// -- CA NAMESPACE -----------------------------------------------------------

namespace ca {


	// -- SYS ----------------------------------------------------------------

	namespace sys {


		// -- EPOLL WAIT -----------------------------------------------------

		/* @DOC Waits until a descriptor epfd watches reports events, or timeout_ns passes (negative:
		   forever, 0: no wait); fills the first entries of reported (the events and the data
		   given at registration) and returns how many, 0 on timeout. Nanosecond timeouts through
		   epoll_pwait2, milliseconds rounded up without it. An empty span fails with
		   errc::invalid_argument. Interrupted by a signal: errc::interrupted, left to the caller,
		   who knows the remaining time. */
		[[nodiscard]] auto epoll_wait(int epfd, ca::span<ca::sys::epoll_event> reported, ca::i64 timeout_ns) noexcept -> ca::result<ca::usz>;

	} // namespace sys

} // namespace ca

#endif // ca_system_io_epoll_wait_hpp
