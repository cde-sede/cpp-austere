#include "3-system/net/impl/sockaddr_unix.hpp"

#include <stddef.h>
#include <sys/socket.h>
#include <sys/un.h>


static_assert(sizeof(ca::sys::impl::sockaddr_unix) == sizeof(struct sockaddr_un));
static_assert(offsetof(ca::sys::impl::sockaddr_unix, family) == offsetof(struct sockaddr_un, sun_family));
static_assert(sizeof(ca::sys::impl::sockaddr_unix::family) == sizeof(sa_family_t));
static_assert(offsetof(ca::sys::impl::sockaddr_unix, path) == offsetof(struct sockaddr_un, sun_path));
static_assert(sizeof(ca::sys::impl::sockaddr_unix::path) == sizeof(sockaddr_un::sun_path));
static_assert(ca::sys::impl::address_family_unix == AF_UNIX);
static_assert(ca::sys::impl::socket_stream == SOCK_STREAM);
