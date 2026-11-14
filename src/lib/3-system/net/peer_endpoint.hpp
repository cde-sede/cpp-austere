#ifndef ca_system_net_peer_endpoint_hpp
#define ca_system_net_peer_endpoint_hpp

#include "2-vocabulary/result.hpp"
#include "3-system/net/ipv4_endpoint.hpp"


// -- CA NAMESPACE -----------------------------------------------------------

namespace ca {


	// -- SYS ----------------------------------------------------------------

	namespace sys {


		// -- PEER ENDPOINT --------------------------------------------------

		/* @DOC The endpoint connected socket fd talks to (getpeername). errc::not_connected when it
		   is not; a socket that is not IPv4 fails with errc::address_family_not_supported. */
		[[nodiscard]] auto peer_endpoint(int fd) noexcept -> ca::result<ca::sys::ipv4_endpoint>;

	} // namespace sys

} // namespace ca

#endif // ca_system_net_peer_endpoint_hpp
