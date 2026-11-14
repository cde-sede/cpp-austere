#ifndef ca_system_net_impl_socket_constants_hpp
#define ca_system_net_impl_socket_constants_hpp

#include "1-primitives/config/platform.hpp"


// -- CA NAMESPACE -----------------------------------------------------------

namespace ca {


	// -- SYS ----------------------------------------------------------------

	namespace sys {


		// -- SOCKET CONSTANTS -----------------------------------------------

		namespace impl {

			/* @DOC IPPROTO_TCP, TCP_NODELAY and the SHUT_* values; checked against the system
			   headers. */
			inline constexpr int protocol_tcp = 6;
			inline constexpr int tcp_no_delay = 1;
			inline constexpr int shut_read = 0;
			inline constexpr int shut_write = 1;
			inline constexpr int shut_both = 2;

			/* @DOC SOL_SOCKET and the SO_* options ca sets or reads. */
#if ca_platform(linux)
			inline constexpr int socket_level = 1;
			inline constexpr int socket_reuse_address = 2;
			inline constexpr int socket_error = 4;
			inline constexpr int socket_keep_alive = 9;
			inline constexpr int socket_reuse_port = 15;
#else
			inline constexpr int socket_level = 0xFFFF;
			inline constexpr int socket_reuse_address = 0x4;
			inline constexpr int socket_error = 0x1007;
			inline constexpr int socket_keep_alive = 0x8;
			inline constexpr int socket_reuse_port = 0x200;
#endif

		} // namespace impl

	} // namespace sys

} // namespace ca

#endif // ca_system_net_impl_socket_constants_hpp
