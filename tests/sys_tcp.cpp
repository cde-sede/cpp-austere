// expect: exit 0
// link: both
// TCP over loopback: listen, connect, accept, endpoints, options, half close; nonblocking accept
// and connect (try_again, in_progress then socket_error); refusals and clashes; unix_listen.
#include "1-primitives/integers/usz.hpp"
#include "2-vocabulary/span.hpp"
#include "2-vocabulary/sv/str.hpp"
#include "3-system/error/errc.hpp"
#include "3-system/file/close.hpp"
#include "3-system/file/read.hpp"
#include "3-system/file/write_all.hpp"
#include "3-system/fs/options.hpp"
#include "3-system/io/poll.hpp"
#include "3-system/io/poll_events.hpp"
#include "3-system/net/accept.hpp"
#include "3-system/net/connect.hpp"
#include "3-system/net/ipv4_endpoint.hpp"
#include "3-system/net/listen.hpp"
#include "3-system/net/local_endpoint.hpp"
#include "3-system/net/peer_endpoint.hpp"
#include "3-system/net/set_socket_option.hpp"
#include "3-system/net/shutdown.hpp"
#include "3-system/net/socket_error.hpp"
#include "3-system/net/tcp_connect.hpp"
#include "3-system/net/tcp_listen.hpp"
#include "3-system/net/tcp_socket.hpp"
#include "3-system/net/unix_connect.hpp"
#include "3-system/net/unix_listen.hpp"

namespace {

	constexpr int CLOEXEC = ca::fs::cloexec_t::OPEN_VALUE;
	constexpr int NONBLOCK = ca::fs::nonblock_t::OPEN_VALUE;

	int sockets[16];
	ca::usz opened = 0U;

	auto keep(const int fd) noexcept -> int {
		sockets[opened++] = fd;
		return fd;
	}

	auto finish(const int code) noexcept -> int {
		bool closed = true;
		while (opened != 0U)
			closed = static_cast<bool>(ca::sys::close(sockets[--opened])) && closed;
		return code == 0 && !closed ? 99 : code;
	}

	auto wait_writable(const int fd) noexcept -> bool {
		ca::sys::poll_entry entry[1] = {ca::sys::poll_entry{fd, ca::sys::poll_events::writable}};
		auto ready = ca::sys::poll(entry, 5000);
		return ready && *ready == 1U;
	}

} // namespace

auto main(void) -> int {
	auto listener = ca::sys::tcp_listen(ca::sys::ipv4_endpoint::loopback(0U), -1, CLOEXEC);
	if (!listener)
		return finish(1);
	keep(*listener);
	auto address = ca::sys::local_endpoint(*listener);
	if (!address || address->port() == 0U || address->address() != 0x7F000001U)
		return finish(2);

	auto client = ca::sys::tcp_connect(*address, CLOEXEC);
	if (!client)
		return finish(3);
	keep(*client);
	auto peer = ca::sys::accept(*listener, CLOEXEC);
	if (!peer)
		return finish(4);
	keep(*peer);
	auto client_peer = ca::sys::peer_endpoint(*client);
	auto client_local = ca::sys::local_endpoint(*client);
	auto server_peer = ca::sys::peer_endpoint(*peer);
	if (!client_peer || !(*client_peer == *address) || !client_local || !server_peer || !(*server_peer == *client_local))
		return finish(5);
	if (!ca::sys::set_socket_option(*client, ca::sys::socket_option::no_delay, true)
	 || !ca::sys::set_socket_option(*client, ca::sys::socket_option::keep_alive, true)
	 || !ca::sys::socket_error(*client))
		return finish(6);

	const ca::byte ping[] = {'p', 'i', 'n', 'g'};
	if (!ca::sys::write_all(*client, ping))
		return finish(7);
	ca::byte buffer[16];
	auto got = ca::sys::read(*peer, buffer);
	if (!got || *got != 4U || buffer[3] != 'g')
		return finish(8);
	if (!ca::sys::shutdown(*client, ca::sys::shutdown_side::write))
		return finish(9);
	got = ca::sys::read(*peer, buffer);
	if (!got || *got != 0U)
		return finish(10);
	if (!ca::sys::write_all(*peer, ca::span<const ca::byte>{ping, 2U}))
		return finish(11);
	got = ca::sys::read(*client, buffer);
	if (!got || *got != 2U || buffer[1] != 'i')
		return finish(12);

	auto quiet = ca::sys::tcp_listen(ca::sys::ipv4_endpoint::loopback(0U), 4, CLOEXEC | NONBLOCK);
	if (!quiet)
		return finish(13);
	keep(*quiet);
	auto nobody = ca::sys::accept(*quiet, 0);
	if (nobody || nobody.error() != ca::errc::try_again)
		return finish(14);
	auto quiet_address = ca::sys::local_endpoint(*quiet);
	if (!quiet_address)
		return finish(15);
	auto pending = ca::sys::tcp_connect(*quiet_address, CLOEXEC | NONBLOCK);
	if (!pending)
		return finish(16);
	keep(*pending);
	if (!wait_writable(*pending) || !ca::sys::socket_error(*pending))
		return finish(17);
	auto arrived = ca::sys::accept(*quiet, CLOEXEC);
	if (!arrived)
		return finish(18);
	keep(*arrived);

	auto clash = ca::sys::tcp_listen(*address, 4, CLOEXEC);
	if (clash || clash.error() != ca::errc::address_in_use)
		return finish(19);
	auto gone = ca::sys::tcp_listen(ca::sys::ipv4_endpoint::loopback(0U), 4, CLOEXEC);
	if (!gone)
		return finish(20);
	auto gone_address = ca::sys::local_endpoint(*gone);
	if (!gone_address || !ca::sys::close(*gone))
		return finish(21);
	auto refused = ca::sys::tcp_connect(*gone_address, CLOEXEC);
	if (refused || refused.error() != ca::errc::connection_refused)
		return finish(22);
	auto refused_later = ca::sys::tcp_connect(*gone_address, CLOEXEC | NONBLOCK);
	if (!refused_later)
		return finish(23);
	keep(*refused_later);
	if (!wait_writable(*refused_later))
		return finish(24);
	auto why = ca::sys::socket_error(*refused_later);
	if (why || why.error() != ca::errc::connection_refused || !ca::sys::socket_error(*refused_later))
		return finish(25);

	auto loose = ca::sys::tcp_socket(CLOEXEC);
	if (!loose)
		return finish(26);
	keep(*loose);
	auto unconnected = ca::sys::peer_endpoint(*loose);
	if (unconnected || unconnected.error() != ca::errc::not_connected)
		return finish(27);
	auto half = ca::sys::shutdown(*loose, ca::sys::shutdown_side::both);
	if (half || half.error() != ca::errc::not_connected)
		return finish(28);
	auto bad_flags = ca::sys::accept(*listener, 0x40000000);
	if (bad_flags || bad_flags.error() != ca::errc::invalid_argument)
		return finish(29);
	auto bad_socket = ca::sys::tcp_socket(0x40000000);
	if (bad_socket || bad_socket.error() != ca::errc::invalid_argument)
		return finish(30);

	const char name[] = "\0ca-sys-tcp-test-unix";
	const ca::sv::str path{name, sizeof(name) - 1U};
	auto local = ca::sys::unix_listen(path, 4, CLOEXEC);
	if (!local)
		return finish(31);
	keep(*local);
	auto taken = ca::sys::unix_listen(path, 4, CLOEXEC);
	if (taken || taken.error() != ca::errc::address_in_use)
		return finish(32);
	auto local_client = ca::sys::unix_connect(path, CLOEXEC);
	if (!local_client)
		return finish(33);
	keep(*local_client);
	auto local_peer = ca::sys::accept(*local, CLOEXEC);
	if (!local_peer)
		return finish(34);
	keep(*local_peer);
	if (!ca::sys::write_all(*local_client, ping))
		return finish(35);
	got = ca::sys::read(*local_peer, buffer);
	if (!got || *got != 4U)
		return finish(36);
	auto not_ipv4 = ca::sys::peer_endpoint(*local_peer);
	if (not_ipv4 || not_ipv4.error() != ca::errc::address_family_not_supported)
		return finish(37);
	auto empty_path = ca::sys::unix_listen(ca::sv::str{}, 4, 0);
	if (empty_path || empty_path.error() != ca::errc::name_too_long)
		return finish(38);
	return finish(0);
}
