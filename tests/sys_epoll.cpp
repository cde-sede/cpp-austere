// expect: exit 0
// link: both
// epoll over a pipe: registration, data, level and edge triggering, modify, remove, errors,
// nanosecond timeouts; eventfd counters as wakeups.
#include "1-primitives/integers/i64.hpp"
#include "1-primitives/integers/u64.hpp"
#include "2-vocabulary/span.hpp"
#include "2-vocabulary/sv/str.hpp"
#include "3-system/error/errc.hpp"
#include "3-system/file/close.hpp"
#include "3-system/file/read.hpp"
#include "3-system/file/write.hpp"
#include "3-system/fs/options.hpp"
#include "3-system/io/epoll_add.hpp"
#include "3-system/io/epoll_create.hpp"
#include "3-system/io/epoll_event.hpp"
#include "3-system/io/epoll_events.hpp"
#include "3-system/io/epoll_modify.hpp"
#include "3-system/io/epoll_remove.hpp"
#include "3-system/io/epoll_wait.hpp"
#include "3-system/io/eventfd.hpp"
#include "3-system/io/eventfd_read.hpp"
#include "3-system/io/eventfd_write.hpp"
#include "3-system/io/pipe.hpp"
#include "3-system/time/clock.hpp"
#include "3-system/time/now.hpp"

namespace e = ca::sys::epoll_events;

static_assert(sizeof(ca::sys::epoll_event) == 12U);

auto main(void) -> int {
	constexpr int CLOEXEC = ca::fs::cloexec_t::OPEN_VALUE;
	constexpr int NONBLOCK = ca::fs::nonblock_t::OPEN_VALUE;
	auto ep = ca::sys::epoll_create(CLOEXEC);
	if (!ep)
		return 1;
	auto ends = ca::sys::pipe(CLOEXEC | NONBLOCK);
	if (!ends)
		return 2;
	ca::sys::epoll_event reported[4];
	if (!ca::sys::epoll_add(*ep, ends->read_end(), e::readable, 42U))
		return 3;
	auto ready = ca::sys::epoll_wait(*ep, reported, 0);
	if (!ready || *ready != 0U)
		return 4;
	if (!ca::sys::write(ends->write_end(), ca::sv::str{"ab"}))
		return 5;
	ready = ca::sys::epoll_wait(*ep, reported, -1);
	if (!ready || *ready != 1U || reported[0].data() != 42U || !reported[0].has(e::readable) || reported[0].has(e::writable))
		return 6;
	ready = ca::sys::epoll_wait(*ep, reported, 0);
	if (!ready || *ready != 1U)
		return 7;

	if (!ca::sys::epoll_modify(*ep, ends->read_end(), e::readable | e::edge, 7U))
		return 8;
	ready = ca::sys::epoll_wait(*ep, reported, 0);
	if (!ready || *ready != 1U || reported[0].data() != 7U)
		return 9;
	ready = ca::sys::epoll_wait(*ep, reported, 0);
	if (!ready || *ready != 0U)
		return 10;
	if (!ca::sys::write(ends->write_end(), ca::sv::str{"c"}))
		return 11;
	ready = ca::sys::epoll_wait(*ep, reported, 0);
	if (!ready || *ready != 1U)
		return 12;

	auto twice = ca::sys::epoll_add(*ep, ends->read_end(), e::readable, 0U);
	if (twice || twice.error() != ca::errc::exists)
		return 13;
	if (!ca::sys::epoll_remove(*ep, ends->read_end()))
		return 14;
	ready = ca::sys::epoll_wait(*ep, reported, 0);
	if (!ready || *ready != 0U)
		return 15;
	auto again = ca::sys::epoll_remove(*ep, ends->read_end());
	if (again || again.error() != ca::errc::no_entry)
		return 16;
	auto unknown = ca::sys::epoll_modify(*ep, ends->read_end(), e::readable, 0U);
	if (unknown || unknown.error() != ca::errc::no_entry)
		return 17;
	auto empty = ca::sys::epoll_wait(*ep, ca::span<ca::sys::epoll_event>{}, 0);
	if (empty || empty.error() != ca::errc::invalid_argument)
		return 18;
	auto bad_flags = ca::sys::epoll_create(NONBLOCK);
	if (bad_flags || bad_flags.error() != ca::errc::invalid_argument)
		return 19;

	const ca::i64 before = ca::sys::now(ca::sys::clock::monotonic);
	ready = ca::sys::epoll_wait(*ep, reported, 2000000);
	const ca::i64 waited = ca::sys::now(ca::sys::clock::monotonic) - before;
	if (!ready || *ready != 0U || waited < 2000000)
		return 20;

	auto counter = ca::sys::eventfd(0U, CLOEXEC | NONBLOCK);
	if (!counter)
		return 21;
	auto nothing = ca::sys::eventfd_read(*counter);
	if (nothing || nothing.error() != ca::errc::try_again)
		return 22;
	if (!ca::sys::epoll_add(*ep, *counter, e::readable, 0xFFFFFFFFFFFFFFFFU))
		return 23;
	if (!ca::sys::eventfd_write(*counter, 3U) || !ca::sys::eventfd_write(*counter, 4U))
		return 24;
	ready = ca::sys::epoll_wait(*ep, reported, -1);
	if (!ready || *ready != 1U || reported[0].data() != 0xFFFFFFFFFFFFFFFFU)
		return 25;
	auto taken = ca::sys::eventfd_read(*counter);
	if (!taken || *taken != 7U)
		return 26;
	ready = ca::sys::epoll_wait(*ep, reported, 0);
	if (!ready || *ready != 0U)
		return 27;
	auto bad_counter = ca::sys::eventfd(0U, 0x40000000);
	if (bad_counter || bad_counter.error() != ca::errc::invalid_argument)
		return 28;

	const bool closed = ca::sys::close(*counter) && ca::sys::close(ends->read_end()) && ca::sys::close(ends->write_end()) && ca::sys::close(*ep);
	return closed ? 0 : 29;
}
