#ifndef ca_system_io_eventfd_read_hpp
#define ca_system_io_eventfd_read_hpp

#include "1-primitives/integers/u64.hpp"
#include "2-vocabulary/result.hpp"


// -- CA NAMESPACE -----------------------------------------------------------

namespace ca {


	// -- SYS ----------------------------------------------------------------

	namespace sys {


		// -- EVENTFD READ ---------------------------------------------------

		/* @DOC Takes eventfd counter fd: returns its value and resets it to 0. At 0 it waits, or,
		   nonblocking, fails with errc::try_again; interrupted calls are restarted. */
		[[nodiscard]] auto eventfd_read(int fd) noexcept -> ca::result<ca::u64>;

	} // namespace sys

} // namespace ca

#endif // ca_system_io_eventfd_read_hpp
