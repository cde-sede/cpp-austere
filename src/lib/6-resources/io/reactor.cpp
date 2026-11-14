#include "1-primitives/atomic/memory_order.hpp"
#include "1-primitives/debug/trap.hpp"
#include "1-primitives/integers/u64.hpp"
#include "1-primitives/utility/exchange.hpp"
#include "2-vocabulary/failure.hpp"
#include "3-system/error/errc.hpp"
#include "3-system/file/close.hpp"
#include "3-system/fs/options.hpp"
#include "3-system/io/epoll_add.hpp"
#include "3-system/io/epoll_create.hpp"
#include "3-system/io/epoll_events.hpp"
#include "3-system/io/epoll_modify.hpp"
#include "3-system/io/epoll_remove.hpp"
#include "3-system/io/epoll_wait.hpp"
#include "3-system/io/eventfd.hpp"
#include "3-system/io/eventfd_read.hpp"
#include "3-system/io/eventfd_write.hpp"
#include "3-system/time/clock.hpp"
#include "3-system/time/now.hpp"
#include "6-resources/io/interest.hpp"
#include "6-resources/io/reactor.hpp"


static_assert(ca::io::interest::readable == ca::sys::epoll_events::readable);
static_assert(ca::io::interest::writable == ca::sys::epoll_events::writable);
static_assert(ca::io::interest::edge == ca::sys::epoll_events::edge);

namespace {

	[[noreturn, gnu::cold, gnu::noinline]] auto moved_while_in_use(void) noexcept -> void {
		ca::trap();
	}

	[[noreturn, gnu::cold, gnu::noinline]] auto scheduled_on_another_reactor(void) noexcept -> void {
		ca::trap();
	}

	[[nodiscard]] auto word_of(const ca::io::impl::watch_node& w) noexcept -> ca::u64 {
		return reinterpret_cast<ca::u64>(&w);
	}

} // namespace


// -- LIFECYCLE --------------------------------------------------------------

ca::io::reactor::reactor(void) noexcept
: self{-1, -1} {
}

ca::io::reactor::reactor(const int epoll, const int wake) noexcept
: _epoll{epoll}, _wake{wake}, _watched{0U}, _scheduled{0U}, _timers{nullptr}, _sequence{0U}, _current{nullptr}, _batch_size{0U}, _batch_at{0U}, _posted{nullptr}, _woken{false}, _stopped{false}, _batch{} {
}

ca::io::reactor::reactor(self&& other) noexcept
: self{ca::exchange(other._epoll, -1), ca::exchange(other._wake, -1)} {

	if (other._watched != 0U || other._scheduled != 0U || other._posted.load() != nullptr) [[unlikely]]
		moved_while_in_use();
	_woken.store(other._woken.load());
	_stopped.store(other._stopped.load());
}

[[nodiscard]] auto ca::io::reactor::make(void) noexcept -> ca::result<self> {

	const auto epoll = ca::sys::epoll_create(ca::fs::cloexec_t::OPEN_VALUE);
	if (!epoll) [[unlikely]]
		return epoll.propagate();
	const auto wake = ca::sys::eventfd(0U, ca::fs::cloexec_t::OPEN_VALUE | ca::fs::nonblock_t::OPEN_VALUE);
	if (!wake) [[unlikely]] {
		(void)ca::sys::close(*epoll);
		return wake.propagate();
	}
	const auto added = ca::sys::epoll_add(*epoll, *wake, ca::sys::epoll_events::readable, WAKE);
	if (!added) [[unlikely]] {
		(void)ca::sys::close(*wake);
		(void)ca::sys::close(*epoll);
		return added.propagate();
	}
	return self{*epoll, *wake};
}

[[nodiscard]] auto ca::io::reactor::cleanup(void) noexcept -> ca::result<void> {

	if (!this->is_live())
		return ca::result<void>{};
	if (_watched != 0U || _scheduled != 0U) [[unlikely]]
		return ca::failure{ca::errc::busy};
	ca::io::impl::task_node* abandoned = _posted.exchange(nullptr);
	while (abandoned != nullptr) {
		ca::io::impl::task_node* const next = abandoned->_next;
		abandoned->_next = nullptr;
		abandoned->_queued.store<ca::memory_order::release>(false);
		abandoned = next;
	}
	const auto wake = ca::sys::close(ca::exchange(_wake, -1));
	const auto epoll = ca::sys::close(ca::exchange(_epoll, -1));
	return wake ? epoll : wake;
}


// -- DESCRIPTORS ------------------------------------------------------------

[[nodiscard]] auto ca::io::reactor::watch(ca::io::impl::watch_node& w, const int fd, const ca::u32 interest) noexcept -> ca::result<void> {

	if (w._reactor != nullptr) [[unlikely]]
		return ca::failure{ca::errc::busy};
	const auto added = ca::sys::epoll_add(_epoll, fd, interest, word_of(w));
	if (!added) [[unlikely]]
		return added.propagate();
	w._reactor = this;
	w._fd = fd;
	w._interest = interest;
	++_watched;
	return ca::result<void>{};
}

[[nodiscard]] auto ca::io::reactor::change(ca::io::impl::watch_node& w, const ca::u32 interest) noexcept -> ca::result<void> {

	if (w._reactor != this) [[unlikely]]
		return ca::failure{ca::errc::invalid_argument};
	if (w._interest == interest)
		return ca::result<void>{};
	const auto modified = ca::sys::epoll_modify(_epoll, w._fd, interest, word_of(w));
	if (!modified) [[unlikely]]
		return modified.propagate();
	w._interest = interest;
	return ca::result<void>{};
}

[[nodiscard]] auto ca::io::reactor::unwatch(ca::io::impl::watch_node& w) noexcept -> ca::result<void> {

	if (w._reactor != this) [[unlikely]]
		return ca::failure{ca::errc::invalid_argument};
	const auto removed = ca::sys::epoll_remove(_epoll, w._fd);
	w._reactor = nullptr;
	w._fd = -1;
	w._interest = 0U;
	--_watched;
	if (_current == &w)
		_current = nullptr;
	const ca::u64 word = word_of(w);
	for (ca::usz i = _batch_at; i < _batch_size; ++i)
		if (_batch[i].data() == word)
			_batch[i] = ca::sys::epoll_event{};
	return removed;
}


// -- TIMERS -----------------------------------------------------------------

auto ca::io::reactor::schedule_at(ca::io::impl::timer_node& t, const ca::i64 deadline) noexcept -> void {

	if (t._reactor == this)
		this->_remove(t);
	else if (t._reactor != nullptr) [[unlikely]]
		scheduled_on_another_reactor();
	t._deadline = deadline;
	t._sequence = _sequence++;
	this->_insert(t);
}

auto ca::io::reactor::schedule_after(ca::io::impl::timer_node& t, const ca::i64 delay) noexcept -> void {

	constexpr ca::i64 latest = 0x7FFFFFFFFFFFFFFF;
	const ca::i64 now = ca::sys::now(ca::sys::clock::monotonic);
	this->schedule_at(t, delay > latest - now ? latest : now + delay);
}

auto ca::io::reactor::cancel(ca::io::impl::timer_node& t) noexcept -> bool {

	if (t._reactor != this)
		return false;
	this->_remove(t);
	return true;
}

[[nodiscard]] auto ca::io::reactor::_before(const ca::io::impl::timer_node& a, const ca::io::impl::timer_node& b) noexcept -> bool {
	return a._deadline < b._deadline || (a._deadline == b._deadline && a._sequence < b._sequence);
}

[[nodiscard]] auto ca::io::reactor::_meld(ca::io::impl::timer_node* a, ca::io::impl::timer_node* b) noexcept -> ca::io::impl::timer_node* {

	if (a == nullptr)
		return b;
	if (b == nullptr)
		return a;
	if (_before(*b, *a)) {
		ca::io::impl::timer_node* const first = b;
		b = a;
		a = first;
	}
	b->_previous = a;
	b->_next = a->_child;
	if (a->_child != nullptr)
		a->_child->_previous = b;
	a->_child = b;
	a->_next = nullptr;
	a->_previous = nullptr;
	return a;
}

[[nodiscard]] auto ca::io::reactor::_merge_pairs(ca::io::impl::timer_node* first) noexcept -> ca::io::impl::timer_node* {

	ca::io::impl::timer_node* pairs = nullptr;
	while (first != nullptr) {
		ca::io::impl::timer_node* const a = first;
		ca::io::impl::timer_node* const b = a->_next;
		first = b != nullptr ? b->_next : nullptr;
		a->_next = nullptr;
		a->_previous = nullptr;
		if (b != nullptr) {
			b->_next = nullptr;
			b->_previous = nullptr;
		}
		ca::io::impl::timer_node* const melded = _meld(a, b);
		melded->_next = pairs;
		pairs = melded;
	}
	ca::io::impl::timer_node* root = nullptr;
	while (pairs != nullptr) {
		ca::io::impl::timer_node* const next = pairs->_next;
		pairs->_next = nullptr;
		root = _meld(root, pairs);
		pairs = next;
	}
	return root;
}

auto ca::io::reactor::_insert(ca::io::impl::timer_node& t) noexcept -> void {

	t._child = nullptr;
	t._next = nullptr;
	t._previous = nullptr;
	_timers = _meld(_timers, &t);
	t._reactor = this;
	++_scheduled;
}

auto ca::io::reactor::_remove(ca::io::impl::timer_node& t) noexcept -> void {

	if (&t == _timers)
		_timers = _merge_pairs(t._child);
	else {
		ca::io::impl::timer_node* const previous = t._previous;
		if (previous->_child == &t)
			previous->_child = t._next;
		else
			previous->_next = t._next;
		if (t._next != nullptr)
			t._next->_previous = previous;
		_timers = _meld(_timers, _merge_pairs(t._child));
	}
	t._child = nullptr;
	t._next = nullptr;
	t._previous = nullptr;
	t._reactor = nullptr;
	--_scheduled;
}

[[nodiscard]] auto ca::io::reactor::_fire_timers(void) noexcept -> ca::usz {

	if (_timers == nullptr)
		return 0U;
	const ca::i64 now = ca::sys::now(ca::sys::clock::monotonic);
	const ca::u64 round = _sequence;
	ca::usz fired = 0U;
	while (_timers != nullptr && _timers->_deadline <= now && _timers->_sequence < round) {
		ca::io::impl::timer_node& t = *_timers;
		this->_remove(t);
		t._dispatch(t, *this);
		++fired;
	}
	return fired;
}


// -- THREADS ----------------------------------------------------------------

[[nodiscard]] auto ca::io::reactor::post(ca::io::impl::task_node& t) noexcept -> ca::result<void> {

	if (t._queued.exchange<ca::memory_order::acq_rel>(true))
		return ca::result<void>{};
	ca::io::impl::task_node* head = _posted.load<ca::memory_order::relaxed>();
	do
		t._next = head;
	while (!_posted.compare_exchange_weak(head, &t));
	return this->_signal();
}

[[nodiscard]] auto ca::io::reactor::wake(void) noexcept -> ca::result<void> {
	return this->_signal();
}

auto ca::io::reactor::stop(void) noexcept -> void {

	_stopped.store(true);
	(void)this->_signal();
}

[[nodiscard]] auto ca::io::reactor::_signal(void) noexcept -> ca::result<void> {

	if (_woken.exchange(true))
		return ca::result<void>{};
	const auto written = ca::sys::eventfd_write(_wake, 1U);
	if (!written && written.error() != ca::errc::try_again) [[unlikely]]
		return written.propagate();
	return ca::result<void>{};
}

[[nodiscard]] auto ca::io::reactor::_run_posted(void) noexcept -> ca::usz {

	if (_posted.load<ca::memory_order::relaxed>() == nullptr)
		return 0U;
	ca::io::impl::task_node* stack = _posted.exchange(nullptr);
	ca::io::impl::task_node* ordered = nullptr;
	while (stack != nullptr) {
		ca::io::impl::task_node* const next = stack->_next;
		stack->_next = ordered;
		ordered = stack;
		stack = next;
	}
	ca::usz ran = 0U;
	while (ordered != nullptr) {
		ca::io::impl::task_node& t = *ordered;
		ordered = t._next;
		t._next = nullptr;
		t._queued.store<ca::memory_order::release>(false);
		t._dispatch(t, *this);
		++ran;
	}
	return ran;
}


// -- RUNNING ----------------------------------------------------------------

[[nodiscard]] auto ca::io::reactor::run(void) noexcept -> ca::result<void> {

	for (;;) {
		if (_stopped.exchange(false))
			return ca::result<void>{};
		if (_watched == 0U && _scheduled == 0U && _posted.load<ca::memory_order::acquire>() == nullptr)
			return ca::result<void>{};
		const auto round = this->run_once(-1);
		if (!round && round.error() != ca::errc::interrupted) [[unlikely]]
			return round.propagate();
	}
}

[[nodiscard]] auto ca::io::reactor::run_once(const ca::i64 timeout) noexcept -> ca::result<ca::usz> {

	if (!this->is_live()) [[unlikely]]
		return ca::failure{ca::errc::bad_descriptor};
	ca::i64 wait = timeout;
	if (_posted.load<ca::memory_order::relaxed>() != nullptr)
		wait = 0;
	else if (_timers != nullptr) {
		ca::i64 left = _timers->_deadline - ca::sys::now(ca::sys::clock::monotonic);
		if (left < 0)
			left = 0;
		if (wait < 0 || left < wait)
			wait = left;
	}
	const auto got = ca::sys::epoll_wait(_epoll, _batch, wait);
	if (!got && got.error() != ca::errc::interrupted) [[unlikely]]
		return got.propagate();
	ca::usz ran = 0U;
	_batch_size = got ? *got : 0U;
	for (ca::usz i = 0U; i < _batch_size; ++i) {
		_batch_at = i + 1U;
		const ca::sys::epoll_event event = _batch[i];
		if (event.data() == WAKE) {
			_woken.store(false);
			(void)ca::sys::eventfd_read(_wake);
			continue;
		}
		if (event.data() == 0U)
			continue;
		ca::io::impl::watch_node* const w = reinterpret_cast<ca::io::impl::watch_node*>(event.data());
		_current = w;
		w->_dispatch(*w, *this, event.events());
		_current = nullptr;
		++ran;
	}
	_batch_size = 0U;
	_batch_at = 0U;
	ran += this->_fire_timers();
	ran += this->_run_posted();
	if (!got) [[unlikely]]
		return got.propagate();
	return ran;
}
