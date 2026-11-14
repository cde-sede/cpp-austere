#include "2-vocabulary/failure.hpp"
#include "3-system/error/errc.hpp"
#include "3-system/net/connect.hpp"
#include "3-system/net/impl/open_socket.hpp"
#include "3-system/net/tcp_connect.hpp"
#include "3-system/net/tcp_socket.hpp"


[[nodiscard]] auto ca::sys::tcp_connect(const ca::sys::ipv4_endpoint endpoint, const int flags) noexcept -> ca::result<int> {

	const auto made = ca::sys::tcp_socket(flags);
	if (!made) [[unlikely]]
		return made.propagate();
	const auto connected = ca::sys::connect(*made, endpoint);
	if (connected || connected.error() == ca::errc::in_progress)
		return *made;
	ca::sys::impl::discard_socket(*made);
	return connected.propagate();
}
