#ifndef ca_system_io_eventfd_hpp
#define ca_system_io_eventfd_hpp

#include "1-primitives/integers/u32.hpp"
#include "2-vocabulary/result.hpp"


// -- CA NAMESPACE -----------------------------------------------------------

namespace ca {


	// -- SYS ----------------------------------------------------------------

	namespace sys {


		// -- EVENTFD --------------------------------------------------------

		/* @DOC A new event counter descriptor starting at initial: eventfd_write adds to it,
		   eventfd_read takes it whole; it polls readable while nonzero, so one thread can wake
		   another's poll or epoll. flags: 0, or the OPEN_VALUE of fs::cloexec and fs::nonblock;
		   any other bit fails with errc::invalid_argument. Linux only: errc::not_supported
		   elsewhere. The caller closes it with sys::close. */
		[[nodiscard]] auto eventfd(ca::u32 initial, int flags) noexcept -> ca::result<int>;

	} // namespace sys

} // namespace ca

#endif // ca_system_io_eventfd_hpp
