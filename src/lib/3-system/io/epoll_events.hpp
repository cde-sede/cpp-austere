#ifndef ca_system_io_epoll_events_hpp
#define ca_system_io_epoll_events_hpp

#include "1-primitives/integers/u32.hpp"


// -- CA NAMESPACE -----------------------------------------------------------

namespace ca {


	// -- SYS ----------------------------------------------------------------

	namespace sys {


		// -- EPOLL EVENTS ---------------------------------------------------

		/* @DOC epoll event bits, without <sys/epoll.h>; checked against it when the library builds.
		   readable to peer_closed are requested and reported (error and hangup are always
		   reported, requested or not); edge, oneshot, exclusive and wakeup change how a
		   registration reports: edge only on changes (drain until errc::try_again), oneshot once
		   then disabled until epoll_modify, exclusive one waiter woken among several epolls. */
		namespace epoll_events {

			inline constexpr ca::u32 readable    = 0x1U;
			inline constexpr ca::u32 urgent      = 0x2U;
			inline constexpr ca::u32 writable    = 0x4U;
			inline constexpr ca::u32 error       = 0x8U;
			inline constexpr ca::u32 hangup      = 0x10U;
			inline constexpr ca::u32 peer_closed = 0x2000U;
			inline constexpr ca::u32 exclusive   = 1U << 28U;
			inline constexpr ca::u32 wakeup      = 1U << 29U;
			inline constexpr ca::u32 oneshot     = 1U << 30U;
			inline constexpr ca::u32 edge        = 1U << 31U;

		} // namespace epoll_events

	} // namespace sys

} // namespace ca

#endif // ca_system_io_epoll_events_hpp
