#ifndef ca_system_net_impl_sockaddr_unix_hpp
#define ca_system_net_impl_sockaddr_unix_hpp

#include "1-primitives/config/platform.hpp"
#include "1-primitives/integers/u16.hpp"
#include "1-primitives/integers/u8.hpp"
#include "1-primitives/integers/usz.hpp"
#include "2-vocabulary/failure.hpp"
#include "2-vocabulary/result.hpp"
#include "2-vocabulary/sv/str.hpp"
#include "3-system/error/errc.hpp"


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

			/* @DOC Fills out with path ('\0' first: an abstract name); returns the length to pass
			   with it, which counts the path without a terminating zero. errc::name_too_long when
			   path is empty or does not fit. */
			[[nodiscard]] constexpr auto to_sockaddr(const ca::sv::str path, ca::sys::impl::sockaddr_unix& out) noexcept -> ca::result<ca::usz> {

				if (path.size() == 0U || path.size() >= ca::sys::impl::unix_path_capacity) [[unlikely]]
					return ca::failure{ca::errc::name_too_long};
				out = ca::sys::impl::sockaddr_unix{};
#if !ca_platform(linux)
				out.length = static_cast<ca::u8>(sizeof(out));
#endif
				out.family = static_cast<decltype(out.family)>(ca::sys::impl::address_family_unix);
				for (ca::usz i = 0U; i < path.size(); ++i)
					out.path[i] = path.data()[i];
				return __builtin_offsetof(ca::sys::impl::sockaddr_unix, path) + path.size();
			}

		} // namespace impl

	} // namespace sys

} // namespace ca

#endif // ca_system_net_impl_sockaddr_unix_hpp
