// expect: exit 0
#include "1-primitives/integers/usz.hpp"
#include "2-vocabulary/span.hpp"
#include "3-system/error/errc.hpp"
#include "3-system/file/close.hpp"
#include "3-system/fs/options.hpp"
#include "3-system/net/bind.hpp"
#include "3-system/net/impl/sockaddr_ipv4.hpp"
#include "3-system/net/ipv4_endpoint.hpp"
#include "3-system/net/local_endpoint.hpp"
#include "3-system/net/receive_from.hpp"
#include "3-system/net/send_to.hpp"
#include "3-system/net/udp_socket.hpp"

static_assert(ca::sys::ipv4_endpoint{192, 168, 1, 20, 9000}.address() == 0xC0A80114U);
static_assert(ca::sys::ipv4_endpoint{192, 168, 1, 20, 9000}.octet(3U) == 20U);
static_assert(ca::sys::ipv4_endpoint::loopback(7U) == ca::sys::ipv4_endpoint{127, 0, 0, 1, 7U});
static_assert(ca::sys::impl::from_sockaddr(ca::sys::impl::to_sockaddr(ca::sys::ipv4_endpoint{10, 1, 2, 3, 0xABCDU})) == ca::sys::ipv4_endpoint{10, 1, 2, 3, 0xABCDU});
static_assert(ca::sys::impl::to_sockaddr(ca::sys::ipv4_endpoint{10, 1, 2, 3, 0xABCDU}).port[0] == 0xABU);

namespace {

	int sockets[4];
	ca::usz opened = 0U;

	auto keep(const int fd) noexcept -> int {
		sockets[opened++] = fd;
		return fd;
	}

	auto finish(const int code) noexcept -> int {
		bool closed = true;
		while (opened != 0U)
			closed = static_cast<bool>(ca::sys::close(sockets[--opened])) && closed;
		return code == 0 && !closed ? 13 : code;
	}

} // namespace

auto main(void) -> int {
	auto server = ca::sys::udp_socket(0);
	if (!server)
		return finish(1);
	keep(*server);
	auto client = ca::sys::udp_socket(0);
	if (!client)
		return finish(1);
	keep(*client);
	auto bound = ca::sys::bind(*server, ca::sys::ipv4_endpoint::loopback(0U));
	if (!bound)
		return finish(2);
	auto address = ca::sys::local_endpoint(*server);
	if (!address || address->port() == 0U || address->address() != 0x7F000001U)
		return finish(3);
	auto taken = ca::sys::udp_socket(0);
	if (!taken)
		return finish(4);
	keep(*taken);
	auto clash = ca::sys::bind(*taken, *address);
	if (clash)
		return finish(5);
	const ca::byte message[] = {1U, 2U, 3U, 4U, 5U};
	for (ca::byte i = 0U; i < 3U; ++i) {
		ca::byte packet[5] = {message[0], message[1], message[2], message[3], i};
		auto sent = ca::sys::send_to(*client, packet, *address);
		if (!sent || *sent != 5U)
			return finish(6);
	}
	ca::byte buffer[3];
	auto cut = ca::sys::receive_from(*server, buffer);
	if (!cut || cut->size != 3U || !cut->truncated || buffer[2] != 3U || cut->from.address() != 0x7F000001U || cut->from.port() == 0U)
		return finish(7);
	ca::byte whole[64];
	auto second = ca::sys::receive_from(*server, whole);
	if (!second || second->size != 5U || second->truncated || whole[4] != 1U)
		return finish(8);
	auto client_address = ca::sys::local_endpoint(*client);
	if (!client_address || !(second->from.port() == client_address->port()))
		return finish(9);
	auto third = ca::sys::receive_from(*server, whole);
	if (!third || third->size != 5U || third->truncated || whole[4] != 2U)
		return finish(10);
	auto odd = ca::sys::udp_socket(ca::fs::create_t::OPEN_VALUE);
	if (odd) {
		keep(*odd);
		return finish(14);
	}
	if (odd.error() != ca::errc::invalid_argument)
		return finish(14);
	auto quiet = ca::sys::udp_socket(ca::fs::nonblock_t::OPEN_VALUE);
	if (!quiet)
		return finish(11);
	keep(*quiet);
	auto nothing = ca::sys::receive_from(*quiet, whole);
	if (nothing || nothing.error() != ca::errc::try_again)
		return finish(12);
	return finish(0);
}
