#include "3-system/net/impl/sockaddr_ipv4.hpp"

#include <netinet/in.h>
#include <stddef.h>
#include <sys/socket.h>


static_assert(sizeof(ca::sys::impl::sockaddr_ipv4) == sizeof(struct sockaddr_in));
static_assert(offsetof(ca::sys::impl::sockaddr_ipv4, family) == offsetof(struct sockaddr_in, sin_family));
static_assert(sizeof(ca::sys::impl::sockaddr_ipv4::family) == sizeof(sa_family_t));
#if !ca_platform(linux)
static_assert(offsetof(ca::sys::impl::sockaddr_ipv4, length) == offsetof(struct sockaddr_in, sin_len));
static_assert(sizeof(ca::sys::impl::sockaddr_ipv4::length) == sizeof(sockaddr_in::sin_len));
#endif
static_assert(offsetof(ca::sys::impl::sockaddr_ipv4, port) == offsetof(struct sockaddr_in, sin_port));
static_assert(offsetof(ca::sys::impl::sockaddr_ipv4, address) == offsetof(struct sockaddr_in, sin_addr));
static_assert(ca::sys::impl::address_family_ipv4 == AF_INET);
static_assert(ca::sys::impl::socket_datagram == SOCK_DGRAM);
static_assert(ca::sys::impl::protocol_udp == IPPROTO_UDP);
