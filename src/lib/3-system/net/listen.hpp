#ifndef ca_system_net_listen_hpp
#define ca_system_net_listen_hpp

#include "2-vocabulary/result.hpp"


// -- CA NAMESPACE -----------------------------------------------------------

namespace ca {


	// -- SYS ----------------------------------------------------------------

	namespace sys {


		// -- LISTEN ---------------------------------------------------------

		/* @DOC Makes stream socket fd accept connections, up to backlog of them queued before
		   accept takes them (the system caps it; negative: the cap). An unbound socket gets a port
		   the system picks. */
		[[nodiscard]] auto listen(int fd, int backlog) noexcept -> ca::result<void>;

	} // namespace sys

} // namespace ca

#endif // ca_system_net_listen_hpp
