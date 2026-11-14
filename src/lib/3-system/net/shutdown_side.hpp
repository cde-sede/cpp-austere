#ifndef ca_system_net_shutdown_side_hpp
#define ca_system_net_shutdown_side_hpp


// -- CA NAMESPACE -----------------------------------------------------------

namespace ca {


	// -- SYS ----------------------------------------------------------------

	namespace sys {


		// -- SHUTDOWN SIDE --------------------------------------------------

		/* @DOC Which direction of a connection shutdown ends. */
		enum class shutdown_side {
			read,
			write,
			both,
		};

	} // namespace sys

} // namespace ca

#endif // ca_system_net_shutdown_side_hpp
