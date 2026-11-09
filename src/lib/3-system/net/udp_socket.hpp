#ifndef ca_system_net_udp_socket_hpp
#define ca_system_net_udp_socket_hpp

#include "2-vocabulary/result.hpp"


// -- CA NAMESPACE -----------------------------------------------------------

namespace ca {


	// -- SYS ----------------------------------------------------------------

	namespace sys {


		// -- UDP SOCKET -----------------------------------------------------

		/* @DOC A new IPv4 UDP socket. flags: 0, or the OPEN_VALUE of fs::cloexec and fs::nonblock
		   (atomically on Linux); any other bit fails with errc::invalid_argument. The caller closes
		   it with sys::close. */
		[[nodiscard]] auto udp_socket(int flags) noexcept -> ca::result<int>;

	} // namespace sys

} // namespace ca

#endif // ca_system_net_udp_socket_hpp
