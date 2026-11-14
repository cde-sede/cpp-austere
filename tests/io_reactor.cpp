// expect: exit 0
// link: both
// ca::io::reactor: a line echo server over io::stream on nonblocking TCP, writable interest
// toggled for a large reply; timer order (deadline, then scheduling), cancel, reschedule,
// periodic, a pairing-heap stress; unwatching within a round; hangups; tasks posted from
// another thread (freestanding only: libc-started programs get no sys::thread), coalescing;
// error cases.
#include "1-primitives/atomic/atomic.hpp"
#include "1-primitives/integers/byte.hpp"
#include "1-primitives/integers/i64.hpp"
#include "1-primitives/integers/u32.hpp"
#include "1-primitives/integers/usz.hpp"
#include "1-primitives/utility/move.hpp"
#include "2-vocabulary/optional.hpp"
#include "2-vocabulary/span.hpp"
#include "2-vocabulary/sv/str.hpp"
#include "3-system/error/errc.hpp"
#include "3-system/file/close.hpp"
#include "3-system/file/read.hpp"
#include "3-system/file/write.hpp"
#include "3-system/file/write_all.hpp"
#include "3-system/fs/options.hpp"
#include "3-system/io/eventfd.hpp"
#include "3-system/io/eventfd_write.hpp"
#include "3-system/io/pipe.hpp"
#include "3-system/io/poll.hpp"
#include "3-system/io/poll_events.hpp"
#include "3-system/net/accept.hpp"
#include "3-system/net/ipv4_endpoint.hpp"
#include "3-system/net/local_endpoint.hpp"
#include "3-system/net/tcp_connect.hpp"
#include "3-system/net/tcp_listen.hpp"
#include "3-system/thread/thread.hpp"
#include "3-system/time/clock.hpp"
#include "3-system/time/now.hpp"
#include "6-resources/io/buffering.hpp"
#include "6-resources/io/interest.hpp"
#include "6-resources/io/reactor.hpp"
#include "6-resources/io/stream.hpp"
#include "6-resources/io/task.hpp"
#include "6-resources/io/timer.hpp"
#include "6-resources/io/watcher.hpp"

namespace {

	constexpr int CLOEXEC = ca::fs::cloexec_t::OPEN_VALUE;
	constexpr int NONBLOCK = ca::fs::nonblock_t::OPEN_VALUE;
	constexpr ca::usz BIG = 1U << 22U;

	// -- echo server --------------------------------------------------------

	struct connection final : ca::io::watcher<connection> {

		int socket;
		char input[64];
		char output[64];
		ca::io::stream stream;
		ca::usz lines{0U};
		ca::usz big_left{0U};
		bool closed{false};
		bool failed{false};

		explicit connection(const int fd) noexcept
		: socket{fd}, input{}, output{}, stream{fd, ca::io::buffering::full, input, output} {
		}

		auto finish(ca::io::reactor& r) noexcept -> void {
			failed = !r.unwatch(*this) || failed;
			failed = !ca::sys::close(socket) || failed;
			closed = true;
		}

		auto on_readable(ca::io::reactor& r) noexcept -> void {
			for (;;) {
				auto line = stream.read_until('\n');
				if (!line) {
					if (line.error() != ca::errc::try_again)
						failed = true;
					return;
				}
				if (line->size() == 0U) {
					finish(r);
					return;
				}
				++lines;
				if (*line == ca::sv::str{"big\n"}) {
					big_left = BIG;
					if (!r.change(*this, ca::io::interest::readable | ca::io::interest::writable))
						failed = true;
					continue;
				}
				if (!stream.write(ca::span<const ca::byte>{reinterpret_cast<const ca::byte*>(line->data()), line->size()}))
					failed = true;
				auto out = stream.write_pending();
				if (!out) {
					stream.clear_error();
					if (out.error() != ca::errc::try_again || !r.change(*this, ca::io::interest::readable | ca::io::interest::writable))
						failed = true;
				}
			}
		}

		auto on_writable(ca::io::reactor& r) noexcept -> void {
			ca::byte chunk[4096];
			for (ca::byte& b : chunk)
				b = 'x';
			while (big_left != 0U) {
				const ca::usz size = big_left < sizeof(chunk) ? big_left : sizeof(chunk);
				auto sent = ca::sys::write(socket, ca::span<const ca::byte>{chunk, size});
				if (!sent) {
					if (sent.error() != ca::errc::try_again)
						failed = true;
					return;
				}
				big_left -= *sent;
			}
			if (!r.change(*this, ca::io::interest::readable))
				failed = true;
		}

	}; // struct connection

	struct listener final : ca::io::watcher<listener> {

		int socket{-1};
		ca::optional<connection>* connections{nullptr};
		ca::usz capacity{0U};
		ca::usz accepted{0U};
		bool failed{false};

		auto on_readable(ca::io::reactor& r) noexcept -> void {
			for (;;) {
				auto fd = ca::sys::accept(socket, CLOEXEC | NONBLOCK);
				if (!fd) {
					failed = fd.error() != ca::errc::try_again || failed;
					return;
				}
				if (accepted == capacity) {
					failed = true;
					(void)ca::sys::close(*fd);
					return;
				}
				connection& c = *connections[accepted].emplace(*fd);
				if (!r.watch(c, *fd, ca::io::interest::readable))
					failed = true;
				++accepted;
			}
		}

	}; // struct listener

	auto readable_now(const int fd) noexcept -> bool {
		ca::sys::poll_entry entry[1] = {ca::sys::poll_entry{fd, ca::sys::poll_events::readable}};
		auto ready = ca::sys::poll(entry, 0);
		return ready && *ready == 1U;
	}

	auto echo(ca::io::reactor& r) noexcept -> int {
		auto server = ca::sys::tcp_listen(ca::sys::ipv4_endpoint::loopback(0U), -1, CLOEXEC | NONBLOCK);
		if (!server)
			return 101;
		auto address = ca::sys::local_endpoint(*server);
		if (!address)
			return 102;
		static ca::optional<connection> connections[2];
		listener l{};
		l.socket = *server;
		l.connections = connections;
		l.capacity = 2U;
		if (!r.watch(l, *server, ca::io::interest::readable))
			return 103;

		auto client = ca::sys::tcp_connect(*address, CLOEXEC);
		if (!client)
			return 104;
		if (!ca::sys::write_all(*client, ca::sv::str{"hello\nworld\n"}))
			return 105;
		char got[64];
		ca::usz have = 0U;
		while (have < 12U) {
			if (!r.run_once(1000000000))
				return 106;
			if (!readable_now(*client))
				continue;
			auto n = ca::sys::read(*client, ca::span<ca::byte>{reinterpret_cast<ca::byte*>(got + have), sizeof(got) - have});
			if (!n || *n == 0U)
				return 107;
			have += *n;
		}
		if (ca::sv::str{got, have} != ca::sv::str{"hello\nworld\n"} || l.accepted != 1U || !connections[0] || connections[0]->lines != 2U)
			return 108;
		connection& c = *connections[0];

		if (!ca::sys::write_all(*client, ca::sv::str{"big\n"}))
			return 109;
		ca::usz received = 0U;
		bool toggled = false;
		while (received < BIG) {
			if (!r.run_once(0))
				return 110;
			toggled = toggled || (c.interest() & ca::io::interest::writable) != 0U;
			if (!readable_now(*client))
				continue;
			ca::byte buffer[65536];
			auto n = ca::sys::read(*client, buffer);
			if (!n || *n == 0U)
				return 111;
			received += *n;
		}
		if (!toggled || !r.run_once(0) || c.interest() != ca::io::interest::readable || c.big_left != 0U)
			return 112;

		if (!ca::sys::close(*client))
			return 113;
		while (!c.closed)
			if (!r.run_once(1000000000))
				return 114;
		if (c.failed || c.is_watched() || l.failed)
			return 115;
		if (!r.unwatch(l) || !ca::sys::close(*server) || r.watched() != 0U)
			return 116;
		return 0;
	}

	// -- timers -------------------------------------------------------------

	ca::usz fired_order[512];
	ca::usz fired_count = 0U;

	struct marker final : ca::io::timer<marker> {

		ca::usz id{0U};
		ca::usz repeats{0U};

		auto on_timer(ca::io::reactor& r) noexcept -> void {
			fired_order[fired_count++] = id;
			if (repeats != 0U) {
				--repeats;
				r.schedule_after(*this, 0);
			}
		}

	}; // struct marker

	auto timers(ca::io::reactor& r) noexcept -> int {
		marker t[6];
		for (ca::usz i = 0U; i < 6U; ++i)
			t[i].id = i;
		const ca::i64 start = ca::sys::now(ca::sys::clock::monotonic);
		r.schedule_at(t[1], start + 3000000);
		r.schedule_at(t[2], start + 1000000);
		r.schedule_at(t[3], start + 2000000);
		r.schedule_at(t[4], start + 2000000);
		r.schedule_at(t[5], start + 1000000);
		if (r.scheduled() != 5U || !t[5].is_scheduled())
			return 201;
		if (!r.cancel(t[5]) || r.cancel(t[5]) || t[5].is_scheduled() || r.scheduled() != 4U)
			return 202;
		r.schedule_after(t[1], 500000);
		if (r.scheduled() != 4U)
			return 203;
		if (!r.run())
			return 204;
		const ca::i64 took = ca::sys::now(ca::sys::clock::monotonic) - start;
		if (fired_count != 4U || fired_order[0] != 1U || fired_order[1] != 2U || fired_order[2] != 3U || fired_order[3] != 4U || took < 2000000)
			return 205;

		fired_count = 0U;
		t[0].repeats = 3U;
		r.schedule_after(t[0], 0);
		ca::usz rounds = 0U;
		while (r.scheduled() != 0U) {
			auto ran = r.run_once(-1);
			if (!ran || *ran != 1U)
				return 206;
			++rounds;
		}
		if (fired_count != 4U || rounds != 4U)
			return 207;

		static marker many[300];
		fired_count = 0U;
		const ca::i64 base = ca::sys::now(ca::sys::clock::monotonic) - 1000000000;
		for (ca::usz i = 0U; i < 300U; ++i) {
			many[i].id = i;
			r.schedule_at(many[i], base + static_cast<ca::i64>((i * 7919U) % 101U) * 1000);
		}
		ca::usz kept = 300U;
		for (ca::usz i = 0U; i < 300U; i += 3U) {
			if (!r.cancel(many[i]))
				return 208;
			--kept;
		}
		for (ca::usz i = 1U; i < 300U; i += 7U)
			if (i % 3U != 0U)
				r.schedule_at(many[i], base + static_cast<ca::i64>((i * 31U) % 53U) * 1000);
		auto ran = r.run_once(0);
		if (!ran || *ran != kept || fired_count != kept || r.scheduled() != 0U)
			return 209;
		for (ca::usz i = 1U; i < kept; ++i) {
			const marker& a = many[fired_order[i - 1U]];
			const marker& b = many[fired_order[i]];
			if (a.deadline() > b.deadline() || fired_order[i] % 3U == 0U)
				return 210;
		}
		return 0;
	}

	// -- unwatching in a round, hangups ---------------------------------------

	struct rival final : ca::io::watcher<rival> {

		rival* other{nullptr};
		ca::usz calls{0U};

		auto on_readable(ca::io::reactor& r) noexcept -> void {
			++calls;
			(void)r.unwatch(*other);
			(void)r.unwatch(*this);
		}

	}; // struct rival

	struct both_ways final : ca::io::watcher<both_ways> {

		ca::usz reads{0U};
		ca::usz writes{0U};

		auto on_readable(ca::io::reactor& r) noexcept -> void {
			++reads;
			(void)r.unwatch(*this);
		}

		auto on_writable(ca::io::reactor&) noexcept -> void {
			++writes;
		}

	}; // struct both_ways

	struct hung final : ca::io::watcher<hung> {

		ca::usz reads{0U};
		ca::usz hangups{0U};

		auto on_readable(ca::io::reactor&) noexcept -> void {
			++reads;
		}

		auto on_hangup(ca::io::reactor& r) noexcept -> void {
			++hangups;
			(void)r.unwatch(*this);
		}

	}; // struct hung

	struct reads_end final : ca::io::watcher<reads_end> {

		int fd{-1};
		ca::usz ends{0U};

		auto on_readable(ca::io::reactor& r) noexcept -> void {
			ca::byte buffer[8];
			auto n = ca::sys::read(fd, buffer);
			if (n && *n == 0U) {
				++ends;
				(void)r.unwatch(*this);
			}
		}

	}; // struct reads_end

	auto rounds(ca::io::reactor& r) noexcept -> int {
		auto a = ca::sys::pipe(CLOEXEC | NONBLOCK);
		auto b = ca::sys::pipe(CLOEXEC | NONBLOCK);
		if (!a || !b || !ca::sys::write(a->write_end(), ca::sv::str{"a"}) || !ca::sys::write(b->write_end(), ca::sv::str{"b"}))
			return 301;
		rival first{};
		rival second{};
		first.other = &second;
		second.other = &first;
		if (!r.watch(first, a->read_end(), ca::io::interest::readable) || !r.watch(second, b->read_end(), ca::io::interest::readable))
			return 302;
		auto ran = r.run_once(-1);
		if (!ran || *ran != 1U || first.calls + second.calls != 1U || r.watched() != 0U)
			return 303;

		auto counter = ca::sys::eventfd(1U, CLOEXEC | NONBLOCK);
		if (!counter)
			return 304;
		both_ways w{};
		if (!r.watch(w, *counter, ca::io::interest::readable | ca::io::interest::writable))
			return 305;
		ran = r.run_once(-1);
		if (!ran || *ran != 1U || w.reads != 1U || w.writes != 0U || w.is_watched())
			return 306;

		hung h{};
		reads_end e{};
		e.fd = b->read_end();
		if (!r.watch(h, a->read_end(), ca::io::interest::readable) || !r.watch(e, b->read_end(), ca::io::interest::none))
			return 307;
		if (!ca::sys::close(a->write_end()) || !ca::sys::close(b->write_end()))
			return 308;
		for (ca::usz i = 0U; i < 4U && r.watched() != 0U; ++i)
			if (!r.run_once(1000000000))
				return 309;
		if (h.hangups != 1U || h.reads != 0U || e.ends != 1U || r.watched() != 0U)
			return 310;
		const bool closed = ca::sys::close(a->read_end()) && ca::sys::close(b->read_end()) && ca::sys::close(*counter);
		return closed ? 0 : 311;
	}

	// -- tasks ----------------------------------------------------------------

	constexpr ca::usz TASKS = 100U;

	ca::usz ran_order[TASKS];
	ca::usz ran_count = 0U;

	struct job final : ca::io::task<job> {

		ca::usz id{0U};
		bool last{false};

		auto on_posted(ca::io::reactor& r) noexcept -> void {
			ran_order[ran_count++] = id;
			if (last)
				r.stop();
		}

	}; // struct job

	struct anchor final : ca::io::watcher<anchor> {

		auto on_readable(ca::io::reactor&) noexcept -> void {
		}

	}; // struct anchor

	struct poster final {

		ca::io::reactor* r;
		job* jobs;
		ca::atomic<int> failures;

	}; // struct poster

	auto post_all(void* argument) noexcept -> void {
		poster& p = *static_cast<poster*>(argument);
		for (ca::usz i = 0U; i < TASKS; ++i)
			if (!p.r->post(p.jobs[i]))
				p.failures.fetch_add(1);
	}

	auto tasks(ca::io::reactor& r) noexcept -> int {
		static job jobs[TASKS];
		for (ca::usz i = 0U; i < TASKS; ++i)
			jobs[i].id = i;
		if (!r.post(jobs[0]) || !jobs[0].is_posted() || !r.post(jobs[0]))
			return 401;
		auto ran = r.run_once(-1);
		if (!ran || *ran != 1U || ran_count != 1U || jobs[0].is_posted())
			return 402;
		if (!r.wake())
			return 403;
		ran = r.run_once(-1);
		if (!ran || *ran != 0U)
			return 404;

		ran_count = 0U;
		jobs[TASKS - 1U].last = true;
		auto idle = ca::sys::eventfd(0U, CLOEXEC | NONBLOCK);
		if (!idle)
			return 405;
		anchor keep{};
		if (!r.watch(keep, *idle, ca::io::interest::readable))
			return 406;
		poster p{&r, jobs, ca::atomic<int>{0}};
		auto worker = ca::sys::thread::make(&post_all, &p);
		if (!worker) {
			if (worker.error() != ca::errc::not_supported)
				return 407;
			post_all(&p);
		}
		if (!r.run())
			return 408;
		if (worker && !worker->join())
			return 409;
		if (p.failures.load() != 0 || ran_count != TASKS)
			return 410;
		for (ca::usz i = 0U; i < TASKS; ++i)
			if (ran_order[i] != i)
				return 411;
		if (!r.unwatch(keep) || !ca::sys::close(*idle))
			return 412;
		return 0;
	}

	// -- errors ---------------------------------------------------------------

	auto errors(ca::io::reactor& r) noexcept -> int {
		auto idle = ca::sys::eventfd(0U, CLOEXEC);
		if (!idle)
			return 501;
		anchor a{};
		anchor b{};
		if (!r.watch(a, *idle, ca::io::interest::readable))
			return 502;
		auto twice = r.watch(a, *idle, ca::io::interest::readable);
		if (twice || twice.error() != ca::errc::busy)
			return 503;
		auto same_fd = r.watch(b, *idle, ca::io::interest::readable);
		if (same_fd || same_fd.error() != ca::errc::exists || b.is_watched())
			return 504;
		auto stranger = r.unwatch(b);
		if (stranger || stranger.error() != ca::errc::invalid_argument)
			return 505;
		auto changed = r.change(b, ca::io::interest::writable);
		if (changed || changed.error() != ca::errc::invalid_argument)
			return 506;
		anchor copy{a};
		if (copy.is_watched() || copy.fd() != -1 || a.fd() != *idle)
			return 507;
		auto busy = r.cleanup();
		if (busy || busy.error() != ca::errc::busy || !r.is_live())
			return 508;
		if (!r.unwatch(a) || !ca::sys::close(*idle))
			return 509;
		r.stop();
		if (!r.run())
			return 510;
		ca::io::reactor dead{};
		auto none = dead.run_once(0);
		if (none || none.error() != ca::errc::bad_descriptor || !dead.cleanup())
			return 511;
		return 0;
	}

} // namespace

auto main(void) -> int {
	auto made = ca::io::reactor::make();
	if (!made)
		return 1;
	ca::io::reactor r{ca::move(*made)};
	if (!r.is_live() || made->is_live())
		return 2;
	int code = echo(r);
	if (code == 0)
		code = timers(r);
	if (code == 0)
		code = rounds(r);
	if (code == 0)
		code = tasks(r);
	if (code == 0)
		code = errors(r);
	if (code != 0)
		return code;
	return r.cleanup() && !r.is_live() ? 0 : 3;
}
