#ifndef ca_system_net_unix_connect_hpp
#define ca_system_net_unix_connect_hpp

#include "2-vocabulary/result.hpp"
#include "2-vocabulary/sv/str.hpp"


// -- CA NAMESPACE -----------------------------------------------------------

namespace ca {


	// -- SYS ----------------------------------------------------------------

	namespace sys {


		// -- UNIX CONNECT ---------------------------------------------------

		/* @DOC A new Unix stream socket connected to the socket at path; a path starting with '\0'
		   names one in Linux's abstract namespace. flags: 0, or the OPEN_VALUE of fs::cloexec and
		   fs::nonblock, as for udp_socket. Connecting to a Unix socket does not wait for the peer:
		   it is done when the call returns, or, with nonblock and the listener's backlog full,
		   fails with errc::try_again. errc::name_too_long when path does not fit a sockaddr_un. The caller closes it with sys::close; read, write_all and
		   poll work on it as on any descriptor. */
		[[nodiscard]] auto unix_connect(ca::sv::str path, int flags) noexcept -> ca::result<int>;

	} // namespace sys

} // namespace ca

#endif // ca_system_net_unix_connect_hpp
