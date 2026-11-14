#ifndef ca_system_net_shutdown_hpp
#define ca_system_net_shutdown_hpp

#include "2-vocabulary/result.hpp"
#include "3-system/net/shutdown_side.hpp"


// -- CA NAMESPACE -----------------------------------------------------------

namespace ca {


	// -- SYS ----------------------------------------------------------------

	namespace sys {


		// -- SHUTDOWN -------------------------------------------------------

		/* @DOC Ends one direction of connected socket fd, or both, for every descriptor of it:
		   write sends the peer an end of stream (its reads return 0) once queued bytes are out;
		   read makes later reads return 0. fd stays open: close it still. errc::not_connected when
		   it is not. */
		[[nodiscard]] auto shutdown(int fd, ca::sys::shutdown_side side) noexcept -> ca::result<void>;

	} // namespace sys

} // namespace ca

#endif // ca_system_net_shutdown_hpp
