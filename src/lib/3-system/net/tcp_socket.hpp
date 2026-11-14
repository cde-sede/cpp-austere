#ifndef ca_system_net_tcp_socket_hpp
#define ca_system_net_tcp_socket_hpp

#include "2-vocabulary/result.hpp"


// -- CA NAMESPACE -----------------------------------------------------------

namespace ca {


	// -- SYS ----------------------------------------------------------------

	namespace sys {


		// -- TCP SOCKET -----------------------------------------------------

		/* @DOC A new IPv4 TCP socket, neither bound nor connected. flags as for udp_socket. The
		   caller closes it with sys::close. */
		[[nodiscard]] auto tcp_socket(int flags) noexcept -> ca::result<int>;

	} // namespace sys

} // namespace ca

#endif // ca_system_net_tcp_socket_hpp
