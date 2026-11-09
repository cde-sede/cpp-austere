#ifndef ca_system_net_send_to_hpp
#define ca_system_net_send_to_hpp

#include "1-primitives/integers/byte.hpp"
#include "1-primitives/integers/usz.hpp"
#include "2-vocabulary/result.hpp"
#include "2-vocabulary/span.hpp"
#include "3-system/net/ipv4_endpoint.hpp"


// -- CA NAMESPACE -----------------------------------------------------------

namespace ca {


	// -- SYS ----------------------------------------------------------------

	namespace sys {


		// -- SEND TO --------------------------------------------------------

		/* @DOC Sends bytes as one datagram to endpoint; returns the bytes sent (all of them for UDP).
		   A nonblocking socket whose buffer is full fails with errc::try_again; interrupted calls
		   are restarted. */
		[[nodiscard]] auto send_to(int fd, ca::span<const ca::byte> bytes, ca::sys::ipv4_endpoint endpoint) noexcept -> ca::result<ca::usz>;

	} // namespace sys

} // namespace ca

#endif // ca_system_net_send_to_hpp
