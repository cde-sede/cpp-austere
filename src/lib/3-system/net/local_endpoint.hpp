#ifndef ca_system_net_local_endpoint_hpp
#define ca_system_net_local_endpoint_hpp

#include "2-vocabulary/result.hpp"
#include "3-system/net/ipv4_endpoint.hpp"


// -- CA NAMESPACE -----------------------------------------------------------

namespace ca {


	// -- SYS ----------------------------------------------------------------

	namespace sys {


		// -- LOCAL ENDPOINT -------------------------------------------------

		/* @DOC The local endpoint socket fd is bound to (getsockname). A socket that is not IPv4
		   fails with errc::address_family_not_supported. */
		[[nodiscard]] auto local_endpoint(int fd) noexcept -> ca::result<ca::sys::ipv4_endpoint>;

	} // namespace sys

} // namespace ca

#endif // ca_system_net_local_endpoint_hpp
