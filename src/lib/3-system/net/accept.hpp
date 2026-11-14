#ifndef ca_system_net_accept_hpp
#define ca_system_net_accept_hpp

#include "2-vocabulary/result.hpp"


// -- CA NAMESPACE -----------------------------------------------------------

namespace ca {


	// -- SYS ----------------------------------------------------------------

	namespace sys {


		// -- ACCEPT ---------------------------------------------------------

		/* @DOC The next connection queued on listening socket fd, as a new connected socket; its
		   peer is peer_endpoint(). flags (the OPEN_VALUE of fs::cloexec and fs::nonblock, as for
		   udp_socket) apply to the new socket, not fd. A nonblocking listener with nothing queued
		   fails with errc::try_again; interrupted calls are restarted; a connection reset while
		   queued fails with errc::connection_aborted, and the next call takes the one after. */
		[[nodiscard]] auto accept(int fd, int flags) noexcept -> ca::result<int>;

	} // namespace sys

} // namespace ca

#endif // ca_system_net_accept_hpp
