#ifndef ca_system_net_connect_hpp
#define ca_system_net_connect_hpp

#include "2-vocabulary/result.hpp"
#include "3-system/net/ipv4_endpoint.hpp"


// -- CA NAMESPACE -----------------------------------------------------------

namespace ca {


	// -- SYS ----------------------------------------------------------------

	namespace sys {


		// -- CONNECT --------------------------------------------------------

		/* @DOC Connects stream socket fd to endpoint (a datagram socket: sets its default peer).
		   Blocking: returns once connected, or with the reason it was not (errc::connection_refused,
		   errc::timed_out, ...); a signal does not stop the connection, which is waited for. A
		   nonblocking socket fails with errc::in_progress while the connection goes on: it is done
		   when fd polls writable, and socket_error(fd) says how it ended. */
		[[nodiscard]] auto connect(int fd, ca::sys::ipv4_endpoint endpoint) noexcept -> ca::result<void>;

	} // namespace sys

} // namespace ca

#endif // ca_system_net_connect_hpp
