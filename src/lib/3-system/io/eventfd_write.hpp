#ifndef ca_system_io_eventfd_write_hpp
#define ca_system_io_eventfd_write_hpp

#include "1-primitives/integers/u64.hpp"
#include "2-vocabulary/result.hpp"


// -- CA NAMESPACE -----------------------------------------------------------

namespace ca {


	// -- SYS ----------------------------------------------------------------

	namespace sys {


		// -- EVENTFD WRITE --------------------------------------------------

		/* @DOC Adds value to eventfd counter fd. Past 2^64 - 2 it waits for a read, or, nonblocking,
		   fails with errc::try_again (the counter as it was: a wake is pending anyway); interrupted
		   calls are restarted. */
		[[nodiscard]] auto eventfd_write(int fd, ca::u64 value) noexcept -> ca::result<void>;

	} // namespace sys

} // namespace ca

#endif // ca_system_io_eventfd_write_hpp
