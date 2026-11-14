/* @DOC Event loop over epoll: descriptors, timers and tasks posted from other threads, calling
   intrusive handlers (ca::io::watcher<Self>, ca::io::timer<Self>, ca::io::task<Self>).

   Performance:
   - Layout: the epoll and eventfd descriptors, counts, the timer heap root, the posted-task stack
     head and a batch of 64 epoll_event (12 bytes each on x86-64): 848 bytes on x86-64, in the
     object. Nothing is allocated: handlers carry their own links, 24 bytes in a watcher, 56 in
     a timer, 24 in a task.
   - A round is one epoll_wait for up to 64 events, then each event's handler through one
     indirect call (the node's address is the epoll word, no lookup), then due timers, then
     posted tasks.
   - watch/change/unwatch: one epoll_ctl each; change skips it when the interest is the same.
     unwatch during a round also clears the node's later events in the batch (at most 63 words
     compared).
   - Timers: a pairing heap through the timers. schedule O(1), the earliest O(1), firing or
     cancelling O(log n) amortized. The nearest deadline is the epoll_pwait2 timeout, in
     nanoseconds: no timerfd, no syscall per timer.
   - post: one CAS onto a lock-free stack, and one eventfd write only when the reactor has not
     been woken since it last took the stack. The reactor takes the whole stack with one
     exchange per round, only when it is not empty. */
#ifndef ca_resources_io_reactor_hpp
#define ca_resources_io_reactor_hpp

#include "1-primitives/atomic/atomic.hpp"
#include "1-primitives/integers/i64.hpp"
#include "1-primitives/integers/u32.hpp"
#include "1-primitives/integers/usz.hpp"
#include "2-vocabulary/result.hpp"
#include "3-system/io/epoll_event.hpp"
#include "6-resources/io/impl/task_node.hpp"
#include "6-resources/io/impl/timer_node.hpp"
#include "6-resources/io/impl/watch_node.hpp"


// -- CA NAMESPACE -----------------------------------------------------------

namespace ca {


	// -- IO -----------------------------------------------------------------

	namespace io {


		template <typename Self>
		class watcher;


		// -- REACTOR --------------------------------------------------------

		/* @DOC An event loop owned by the thread that runs it: it watches descriptors (watch), calls
		   timers when due (schedule_at, schedule_after) and runs tasks other threads post. Every
		   member is for the running thread only, except post, wake and stop, which any thread may
		   call. Handlers are the caller's objects (watcher, timer, task): the reactor allocates
		   nothing and keeps their addresses, so each stays put and alive while watched, scheduled
		   or posted. A handler may watch, unwatch, schedule and cancel anything, itself included,
		   and destroy itself once it unwatched itself, but never runs the reactor (run, run_once)
		   from inside a round. make() then cleanup(); not movable once anything is watched,
		   scheduled or posted. */
		class reactor final {


			private:

				// -- friends ------------------------------------------------

				template <typename Self>
				friend class ca::io::watcher;


				// -- private types ------------------------------------------

				using self = ca::io::reactor;


				// -- private constants --------------------------------------

				static constexpr ca::usz BATCH = 64U;
				static constexpr ca::u64 WAKE = 1U;


				// -- private members ----------------------------------------

				int _epoll;
				int _wake;
				ca::usz _watched;
				ca::usz _scheduled;
				ca::io::impl::timer_node* _timers;
				ca::u64 _sequence;
				ca::io::impl::watch_node* _current;
				ca::usz _batch_size;
				ca::usz _batch_at;
				ca::atomic<ca::io::impl::task_node*> _posted;
				ca::atomic<bool> _woken;
				ca::atomic<bool> _stopped;
				ca::sys::epoll_event _batch[BATCH];


				// -- private lifecycle --------------------------------------

				reactor(int epoll, int wake) noexcept;


			public:

				// -- public lifecycle ---------------------------------------

				/* @DOC Not live: what moving and cleanup() leave. */
				reactor(void) noexcept;

				reactor(const self&) = delete;

				/* @DOC Takes other's descriptors. Traps when other has anything watched, scheduled or
				   posted: their handlers know its address. */
				reactor(self&& other) noexcept;

				/* @DOC A new reactor: an epoll descriptor and an eventfd for wakeups, both
				   close-on-exec. errc::not_supported off Linux. */
				[[nodiscard]] static auto make(void) noexcept -> ca::result<self>;


				// -- assignment operators -----------------------------------

				auto operator=(const self&) -> self& = delete;

				auto operator=(self&&) -> self& = delete;


				// -- accessors ----------------------------------------------

				/* @DOC Whether make() opened it and cleanup() has not closed it. */
				[[nodiscard]] constexpr auto is_live(void) const noexcept -> bool {
					return _epoll != -1;
				}

				/* @DOC Descriptors watched. */
				[[nodiscard]] constexpr auto watched(void) const noexcept -> ca::usz {
					return _watched;
				}

				/* @DOC Timers scheduled. */
				[[nodiscard]] constexpr auto scheduled(void) const noexcept -> ca::usz {
					return _scheduled;
				}

				/* @DOC The epoll descriptor, to nest this reactor in another poll. */
				[[nodiscard]] constexpr auto native(void) const noexcept -> int {
					return _epoll;
				}


				// -- descriptors --------------------------------------------

				/* @DOC Starts watching fd for interest (ca::io::interest bits), calling w's handlers;
				   w must not be watched already (errc::busy). fd stays the caller's: unwatch before
				   closing it. errc::exists when this reactor watches fd through another watcher;
				   errc::not_permitted for a descriptor epoll cannot watch (a regular file). */
				[[nodiscard]] auto watch(ca::io::impl::watch_node& w, int fd, ca::u32 interest) noexcept -> ca::result<void>;

				/* @DOC Replaces what w is watched for, typically adding interest::writable while a
				   writer has bytes pending and dropping it once they are out. No syscall when
				   unchanged. errc::invalid_argument when this reactor does not watch w. */
				[[nodiscard]] auto change(ca::io::impl::watch_node& w, ca::u32 interest) noexcept -> ca::result<void>;

				/* @DOC Stops watching w: none of its handlers is called after this, not even for events
				   of the current round. w is unwatched even on failure (the epoll_ctl error, fd
				   already closed for instance). errc::invalid_argument when this reactor does not
				   watch w. */
				[[nodiscard]] auto unwatch(ca::io::impl::watch_node& w) noexcept -> ca::result<void>;


				// -- timers -------------------------------------------------

				/* @DOC Calls t once sys::now(clock::monotonic) reaches deadline (nanoseconds); a
				   scheduled t moves to the new deadline. Timers due together run by deadline, then in
				   the order they were scheduled. A timer scheduled by a timer of the round, already
				   due, runs in the next round, after I/O. Traps when t is scheduled on another
				   reactor. */
				auto schedule_at(ca::io::impl::timer_node& t, ca::i64 deadline) noexcept -> void;

				/* @DOC schedule_at(t, now + delay), delay in nanoseconds. */
				auto schedule_after(ca::io::impl::timer_node& t, ca::i64 delay) noexcept -> void;

				/* @DOC Unschedules t; whether it was scheduled here. */
				auto cancel(ca::io::impl::timer_node& t) noexcept -> bool;


				// -- threads ------------------------------------------------

				/* @DOC Any thread: runs t on the reactor's thread, in the round after this call at the
				   latest, waking the reactor if it waits. Posting t while posted does nothing: it
				   runs once. Tasks posted from one thread run in the order posted. t stays alive until
				   it has run, and the reactor until every post returned. Fails only when the wakeup
				   cannot be written (the eventfd error); t is posted then all the same. */
				[[nodiscard]] auto post(ca::io::impl::task_node& t) noexcept -> ca::result<void>;

				/* @DOC Any thread: makes a waiting round end now. */
				[[nodiscard]] auto wake(void) noexcept -> ca::result<void>;

				/* @DOC Any thread: run() returns at the end of the current round (at once when called
				   before it). Events already taken from epoll are dispatched first, so none is lost. */
				auto stop(void) noexcept -> void;


				// -- running ------------------------------------------------

				/* @DOC Rounds until stop() or nothing is left to wait for: no descriptor watched, no
				   timer scheduled, no task posted. A thread that expects posts only keeps a watcher
				   or a timer. Interrupted waits are resumed. The first error of a wait returns. */
				[[nodiscard]] auto run(void) noexcept -> ca::result<void>;

				/* @DOC One round: waits up to timeout nanoseconds (negative: forever, 0: no wait) for
				   events, less when a timer is due sooner or a task is posted, then calls their
				   handlers; returns how many handlers ran. errc::interrupted when a signal ended the
				   wait (due timers and posted tasks run first). */
				[[nodiscard]] auto run_once(ca::i64 timeout) noexcept -> ca::result<ca::usz>;


				// -- cleanup ------------------------------------------------

				/* @DOC Closes both descriptors: not live after, even on failure. errc::busy, nothing
				   closed, while descriptors are watched or timers scheduled; posted tasks that have
				   not run never will. */
				[[nodiscard]] auto cleanup(void) noexcept -> ca::result<void>;


			private:

				// -- private methods ----------------------------------------

				[[nodiscard]] constexpr auto _is_current(const ca::io::impl::watch_node& w) const noexcept -> bool {
					return _current == &w;
				}

				[[nodiscard]] auto _signal(void) noexcept -> ca::result<void>;

				[[nodiscard]] static auto _before(const ca::io::impl::timer_node& a, const ca::io::impl::timer_node& b) noexcept -> bool;

				[[nodiscard]] static auto _meld(ca::io::impl::timer_node* a, ca::io::impl::timer_node* b) noexcept -> ca::io::impl::timer_node*;

				[[nodiscard]] static auto _merge_pairs(ca::io::impl::timer_node* first) noexcept -> ca::io::impl::timer_node*;

				auto _insert(ca::io::impl::timer_node& t) noexcept -> void;

				auto _remove(ca::io::impl::timer_node& t) noexcept -> void;

				[[nodiscard]] auto _fire_timers(void) noexcept -> ca::usz;

				[[nodiscard]] auto _run_posted(void) noexcept -> ca::usz;

		}; // class reactor

	} // namespace io

} // namespace ca

#endif // ca_resources_io_reactor_hpp
