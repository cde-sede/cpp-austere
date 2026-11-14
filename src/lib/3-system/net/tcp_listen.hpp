#ifndef ca_system_net_tcp_listen_hpp
#define ca_system_net_tcp_listen_hpp

#include "2-vocabulary/result.hpp"
#include "3-system/net/ipv4_endpoint.hpp"


// -- CA NAMESPACE -----------------------------------------------------------

namespace ca {


	// -- SYS ----------------------------------------------------------------

	namespace sys {


		// -- TCP LISTEN -----------------------------------------------------

		/* @DOC A new TCP socket listening on endpoint (port 0: one the system picks, see
		   local_endpoint), with socket_option::reuse_address on so a restarted server gets its
		   port back: tcp_socket(flags), bind, listen(backlog). errc::address_in_use when another
		   socket listens there. The caller closes it with sys::close. */
		[[nodiscard]] auto tcp_listen(ca::sys::ipv4_endpoint endpoint, int backlog, int flags) noexcept -> ca::result<int>;

	} // namespace sys

} // namespace ca

#endif // ca_system_net_tcp_listen_hpp
