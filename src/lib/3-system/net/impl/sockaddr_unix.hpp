#ifndef ca_system_net_impl_sockaddr_unix_hpp
#define ca_system_net_impl_sockaddr_unix_hpp

#include "1-primitives/config/platform.hpp"
#include "1-primitives/integers/u16.hpp"
#include "1-primitives/integers/u8.hpp"
#include "1-primitives/integers/usz.hpp"


// -- CA NAMESPACE -----------------------------------------------------------

namespace ca {


	// -- SYS ----------------------------------------------------------------

	namespace sys {


		// -- IMPL -----------------------------------------------------------

		namespace impl {


			/* @DOC AF_UNIX and SOCK_STREAM; checked against the system headers. */
			inline constexpr int address_family_unix = 1;
			inline constexpr int socket_stream = 1;

			/* @DOC Bytes of a socket path, its terminating zero included. */
#if ca_platform(linux)
			inline constexpr ca::usz unix_path_capacity = 108U;
#else
			inline constexpr ca::usz unix_path_capacity = 104U;
#endif

			/* @DOC struct sockaddr_un as the platform lays it out, checked against <sys/un.h>. */
			struct sockaddr_unix final {
#if ca_platform(linux)
				ca::u16 family;
#else
				ca::u8 length;
				ca::u8 family;
#endif
				char path[ca::sys::impl::unix_path_capacity];
			}; // struct sockaddr_unix

		} // namespace impl

	} // namespace sys

} // namespace ca

#endif // ca_system_net_impl_sockaddr_unix_hpp
