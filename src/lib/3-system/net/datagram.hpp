#ifndef ca_system_net_datagram_hpp
#define ca_system_net_datagram_hpp

#include "1-primitives/integers/usz.hpp"
#include "3-system/net/ipv4_endpoint.hpp"


// -- CA NAMESPACE -----------------------------------------------------------

namespace ca {


	// -- SYS ----------------------------------------------------------------

	namespace sys {


		// -- DATAGRAM -------------------------------------------------------

		/* @DOC What receive_from got: the bytes written into the buffer, whether the datagram was
		   longer than the buffer (cut to it, the rest lost) and who sent it. */
		struct datagram final {
			ca::usz size;
			bool truncated;
			ca::sys::ipv4_endpoint from;
		}; // struct datagram

	} // namespace sys

} // namespace ca

#endif // ca_system_net_datagram_hpp
