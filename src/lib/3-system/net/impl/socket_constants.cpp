#include "3-system/net/impl/socket_constants.hpp"

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>


static_assert(ca::sys::impl::protocol_tcp == IPPROTO_TCP);
static_assert(ca::sys::impl::tcp_no_delay == TCP_NODELAY);
static_assert(ca::sys::impl::shut_read == SHUT_RD);
static_assert(ca::sys::impl::shut_write == SHUT_WR);
static_assert(ca::sys::impl::shut_both == SHUT_RDWR);
static_assert(ca::sys::impl::socket_level == SOL_SOCKET);
static_assert(ca::sys::impl::socket_reuse_address == SO_REUSEADDR);
static_assert(ca::sys::impl::socket_error == SO_ERROR);
static_assert(ca::sys::impl::socket_keep_alive == SO_KEEPALIVE);
static_assert(ca::sys::impl::socket_reuse_port == SO_REUSEPORT);
