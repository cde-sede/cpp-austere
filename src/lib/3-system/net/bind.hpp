#ifndef ca_system_net_bind_hpp
#define ca_system_net_bind_hpp

#include "2-vocabulary/result.hpp"
#include "3-system/net/ipv4_endpoint.hpp"


// -- CA NAMESPACE -----------------------------------------------------------

namespace ca {


	// -- SYS ----------------------------------------------------------------

	namespace sys {


		// -- BIND -----------------------------------------------------------

		/* @DOC Gives socket fd the local endpoint (port 0: one the system picks, see
		   local_endpoint). */
		[[nodiscard]] auto bind(int fd, ca::sys::ipv4_endpoint endpoint) noexcept -> ca::result<void>;

	} // namespace sys

} // namespace ca

#endif // ca_system_net_bind_hpp
