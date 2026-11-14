#ifndef ca_system_net_impl_sockaddr_ipv4_hpp
#define ca_system_net_impl_sockaddr_ipv4_hpp

#include "1-primitives/config/platform.hpp"
#include "1-primitives/integers/u16.hpp"
#include "1-primitives/integers/u32.hpp"
#include "1-primitives/integers/u8.hpp"
#include "1-primitives/integers/usz.hpp"
#include "2-vocabulary/failure.hpp"
#include "2-vocabulary/result.hpp"
#include "3-system/error/errc.hpp"
#include "3-system/net/ipv4_endpoint.hpp"


// -- CA NAMESPACE -----------------------------------------------------------

namespace ca {


	// -- SYS ----------------------------------------------------------------

	namespace sys {


		// -- SOCKADDR IPV4 --------------------------------------------------

		namespace impl {

			/* @DOC AF_INET, SOCK_DGRAM and IPPROTO_UDP; checked against the system headers. SOCK_STREAM
			   is in sockaddr_unix.hpp, IPPROTO_TCP in socket_constants.hpp. */
			inline constexpr int address_family_ipv4 = 2;
			inline constexpr int socket_datagram = 2;
			inline constexpr int protocol_udp = 17;

			/* @DOC struct sockaddr_in as the platform lays it out, checked against <netinet/in.h>:
			   port and address in network order, byte by byte. */
			struct sockaddr_ipv4 final {
#if ca_platform(linux)
				ca::u16 family;
#else
				ca::u8 length;
				ca::u8 family;
#endif
				ca::u8 port[2];
				ca::u8 address[4];
				ca::u8 zero[8];
			}; // struct sockaddr_ipv4

			/* @DOC endpoint as a sockaddr_ipv4. */
			[[nodiscard]] constexpr auto to_sockaddr(const ca::sys::ipv4_endpoint endpoint) noexcept -> ca::sys::impl::sockaddr_ipv4 {

				ca::sys::impl::sockaddr_ipv4 out{};
#if !ca_platform(linux)
				out.length = static_cast<ca::u8>(sizeof(ca::sys::impl::sockaddr_ipv4));
#endif
				out.family = static_cast<decltype(out.family)>(ca::sys::impl::address_family_ipv4);
				out.port[0] = static_cast<ca::u8>(endpoint.port() >> 8U);
				out.port[1] = static_cast<ca::u8>(endpoint.port());
				for (ca::u32 i = 0U; i < 4U; ++i)
					out.address[i] = endpoint.octet(i);
				return out;
			}

			/* @DOC The endpoint a sockaddr_ipv4 holds. */
			[[nodiscard]] constexpr auto from_sockaddr(const ca::sys::impl::sockaddr_ipv4& address) noexcept -> ca::sys::ipv4_endpoint {
				return ca::sys::ipv4_endpoint{address.address[0], address.address[1], address.address[2], address.address[3],
					static_cast<ca::u16>((static_cast<ca::u16>(address.port[0]) << 8U) | address.port[1])};
			}

			/* @DOC The endpoint in address as the kernel filled length bytes of it; anything but a
			   whole IPv4 address fails with errc::address_family_not_supported. */
			[[nodiscard]] constexpr auto checked_endpoint(const ca::sys::impl::sockaddr_ipv4& address, const ca::usz length) noexcept -> ca::result<ca::sys::ipv4_endpoint> {

				if (length != sizeof(address) || address.family != ca::sys::impl::address_family_ipv4) [[unlikely]]
					return ca::failure{ca::errc::address_family_not_supported};
				return ca::sys::impl::from_sockaddr(address);
			}

		} // namespace impl

	} // namespace sys

} // namespace ca

#endif // ca_system_net_impl_sockaddr_ipv4_hpp
