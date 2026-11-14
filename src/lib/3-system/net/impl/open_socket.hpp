#ifndef ca_system_net_impl_open_socket_hpp
#define ca_system_net_impl_open_socket_hpp

#include "2-vocabulary/result.hpp"


// -- CA NAMESPACE -----------------------------------------------------------

namespace ca {


	// -- SYS ----------------------------------------------------------------

	namespace sys {


		// -- OPEN SOCKET ----------------------------------------------------

		namespace impl {

			/* @DOC socket(family, type, protocol) with flags the OPEN_VALUE of fs::cloexec and
			   fs::nonblock (atomically on Linux, by fcntl elsewhere); any other bit fails with
			   errc::invalid_argument. */
			[[nodiscard]] auto open_socket(int family, int type, int protocol, int flags) noexcept -> ca::result<int>;

			/* @DOC Closes fd after a failure that is returned instead. */
			auto discard_socket(int fd) noexcept -> void;

		} // namespace impl

	} // namespace sys

} // namespace ca

#endif // ca_system_net_impl_open_socket_hpp
