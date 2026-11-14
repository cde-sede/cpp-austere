#ifndef ca_system_net_unix_listen_hpp
#define ca_system_net_unix_listen_hpp

#include "2-vocabulary/result.hpp"
#include "2-vocabulary/sv/str.hpp"


// -- CA NAMESPACE -----------------------------------------------------------

namespace ca {


	// -- SYS ----------------------------------------------------------------

	namespace sys {


		// -- UNIX LISTEN ----------------------------------------------------

		/* @DOC A new Unix stream socket listening at path ('\0' first: an abstract name, gone with
		   the socket), backlog as for listen, flags as for unix_connect. A filesystem path makes a
		   socket file, which stays after close: unlink it; one already there fails with
		   errc::address_in_use. errc::name_too_long when path does not fit a sockaddr_un. The caller
		   closes it with sys::close. */
		[[nodiscard]] auto unix_listen(ca::sv::str path, int backlog, int flags) noexcept -> ca::result<int>;

	} // namespace sys

} // namespace ca

#endif // ca_system_net_unix_listen_hpp
