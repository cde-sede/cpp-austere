#ifndef ca_system_net_socket_error_hpp
#define ca_system_net_socket_error_hpp

#include "2-vocabulary/result.hpp"


// -- CA NAMESPACE -----------------------------------------------------------

namespace ca {


	// -- SYS ----------------------------------------------------------------

	namespace sys {


		// -- SOCKET ERROR ---------------------------------------------------

		/* @DOC Takes socket fd's pending error (SO_ERROR), clearing it: ok when there is none, that
		   error as the failure otherwise. A nonblocking connect that failed with
		   errc::in_progress is done once fd polls writable; this says how it ended. */
		[[nodiscard]] auto socket_error(int fd) noexcept -> ca::result<void>;

	} // namespace sys

} // namespace ca

#endif // ca_system_net_socket_error_hpp
