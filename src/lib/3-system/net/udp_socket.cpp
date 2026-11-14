#include "3-system/net/impl/open_socket.hpp"
#include "3-system/net/impl/sockaddr_ipv4.hpp"
#include "3-system/net/udp_socket.hpp"


[[nodiscard]] auto ca::sys::udp_socket(const int flags) noexcept -> ca::result<int> {
	return ca::sys::impl::open_socket(ca::sys::impl::address_family_ipv4, ca::sys::impl::socket_datagram, ca::sys::impl::protocol_udp, flags);
}
