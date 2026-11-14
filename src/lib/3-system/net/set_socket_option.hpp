#ifndef ca_system_net_set_socket_option_hpp
#define ca_system_net_set_socket_option_hpp

#include "2-vocabulary/result.hpp"
#include "3-system/net/socket_option.hpp"


// -- CA NAMESPACE -----------------------------------------------------------

namespace ca {


	// -- SYS ----------------------------------------------------------------

	namespace sys {


		// -- SET SOCKET OPTION ----------------------------------------------

		/* @DOC Turns option on or off for socket fd (setsockopt). reuse_address and reuse_port take
		   effect at the next bind; no_delay on a socket that is not TCP fails with
		   errc::not_supported or errc::invalid_argument, as the system says. */
		[[nodiscard]] auto set_socket_option(int fd, ca::sys::socket_option option, bool on) noexcept -> ca::result<void>;

	} // namespace sys

} // namespace ca

#endif // ca_system_net_set_socket_option_hpp
