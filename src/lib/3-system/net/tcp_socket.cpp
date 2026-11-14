#include "3-system/net/impl/open_socket.hpp"
#include "3-system/net/impl/sockaddr_ipv4.hpp"
#include "3-system/net/impl/sockaddr_unix.hpp"
#include "3-system/net/impl/socket_constants.hpp"
#include "3-system/net/tcp_socket.hpp"


[[nodiscard]] auto ca::sys::tcp_socket(const int flags) noexcept -> ca::result<int> {
	return ca::sys::impl::open_socket(ca::sys::impl::address_family_ipv4, ca::sys::impl::socket_stream, ca::sys::impl::protocol_tcp, flags);
}
