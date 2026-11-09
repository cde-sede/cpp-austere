#ifndef ca_system_net_receive_from_hpp
#define ca_system_net_receive_from_hpp

#include "1-primitives/integers/byte.hpp"
#include "2-vocabulary/result.hpp"
#include "2-vocabulary/span.hpp"
#include "3-system/net/datagram.hpp"


// -- CA NAMESPACE -----------------------------------------------------------

namespace ca {


	// -- SYS ----------------------------------------------------------------

	namespace sys {


		// -- RECEIVE FROM ---------------------------------------------------

		/* @DOC Receives one datagram into buffer: its size (cut to the buffer), whether it was cut
		   and its sender. A socket that is not IPv4 fails with errc::address_family_not_supported; a
		   nonblocking socket with nothing queued fails with errc::try_again; interrupted calls are
		   restarted. */
		[[nodiscard]] auto receive_from(int fd, ca::span<ca::byte> buffer) noexcept -> ca::result<ca::sys::datagram>;

	} // namespace sys

} // namespace ca

#endif // ca_system_net_receive_from_hpp
