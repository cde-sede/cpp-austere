// expect: exit 0
// A listener in the abstract namespace (unix_listen); unix_connect reaches it, bytes go both ways;
// a missing name is refused, an oversized path rejected.
#include "2-vocabulary/span.hpp"
#include "2-vocabulary/sv/str.hpp"
#include "3-system/error/errc.hpp"
#include "3-system/file/close.hpp"
#include "3-system/file/read.hpp"
#include "3-system/file/write_all.hpp"
#include "3-system/fs/options.hpp"
#include "3-system/net/accept.hpp"
#include "3-system/net/unix_connect.hpp"
#include "3-system/net/unix_listen.hpp"

auto main(void) -> int {
	const char name[] = "\0ca-sys-unix-connect-test";
	const ca::sv::str path{name, sizeof(name) - 1U};
	auto listener = ca::sys::unix_listen(path, 4, ca::fs::cloexec_t::OPEN_VALUE);
	if (!listener)
		return 1;
	auto client = ca::sys::unix_connect(path, ca::fs::cloexec_t::OPEN_VALUE);
	if (!client)
		return 3;
	auto peer = ca::sys::accept(*listener, 0);
	if (!peer)
		return 4;
	const ca::byte hello[] = {'h', 'i', '!'};
	if (!ca::sys::write_all(*client, hello))
		return 5;
	ca::byte buffer[8];
	auto got = ca::sys::read(*peer, buffer);
	if (!got || *got != 3U || buffer[2] != '!')
		return 6;
	if (!ca::sys::write_all(*peer, ca::span<const ca::byte>{hello, 2U}))
		return 7;
	got = ca::sys::read(*client, buffer);
	if (!got || *got != 2U || buffer[1] != 'i')
		return 8;

	const char missing[] = "\0ca-sys-unix-connect-nobody";
	auto refused = ca::sys::unix_connect(ca::sv::str{missing, sizeof(missing) - 1U}, 0);
	if (refused || refused.error() != ca::errc::connection_refused)
		return 9;
	char long_path[200];
	for (char& c : long_path)
		c = 'x';
	auto too_long = ca::sys::unix_connect(ca::sv::str{long_path, sizeof(long_path)}, 0);
	if (too_long || too_long.error() != ca::errc::name_too_long)
		return 10;
	auto bad_flags = ca::sys::unix_connect(path, 0x40000000);
	if (bad_flags || bad_flags.error() != ca::errc::invalid_argument)
		return 11;
	const bool closed = ca::sys::close(*client) && ca::sys::close(*peer) && ca::sys::close(*listener);
	return closed ? 0 : 12;
}
