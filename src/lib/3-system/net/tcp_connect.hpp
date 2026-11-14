#ifndef ca_system_net_tcp_connect_hpp
#define ca_system_net_tcp_connect_hpp

#include "2-vocabulary/result.hpp"
#include "3-system/net/ipv4_endpoint.hpp"


// -- CA NAMESPACE -----------------------------------------------------------

namespace ca {


	// -- SYS ----------------------------------------------------------------

	namespace sys {


		// -- TCP CONNECT ----------------------------------------------------

		/* @DOC A new TCP socket connected to endpoint: tcp_socket(flags) then connect. Without
		   fs::nonblock it returns connected or fails, the socket closed. With it, it returns as soon
		   as the connection is started: the socket polls writable when it is done, and
		   socket_error says how it ended. The caller closes it with sys::close. */
		[[nodiscard]] auto tcp_connect(ca::sys::ipv4_endpoint endpoint, int flags) noexcept -> ca::result<int>;

	} // namespace sys

} // namespace ca

#endif // ca_system_net_tcp_connect_hpp
