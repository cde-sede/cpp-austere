#ifndef ca_system_net_socket_option_hpp
#define ca_system_net_socket_option_hpp


// -- CA NAMESPACE -----------------------------------------------------------

namespace ca {


	// -- SYS ----------------------------------------------------------------

	namespace sys {


		// -- SOCKET OPTION --------------------------------------------------

		/* @DOC On/off socket options for set_socket_option. reuse_address: bind a listener to a port
		   whose old connections still linger (SO_REUSEADDR); reuse_port: several sockets bound to
		   one port, connections spread among them, one listener per thread (SO_REUSEPORT);
		   keep_alive: probe idle connections (SO_KEEPALIVE); no_delay: send small writes at once
		   instead of coalescing them (TCP_NODELAY). */
		enum class socket_option {
			reuse_address,
			reuse_port,
			keep_alive,
			no_delay,
		};

	} // namespace sys

} // namespace ca

#endif // ca_system_net_socket_option_hpp
