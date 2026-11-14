// expect: exit 0
// link: both
#include "3-system/error/errc.hpp"
#include "3-system/process/getpid.hpp"
#include "3-system/process/getuid.hpp"
#include "3-system/process/mask_change.hpp"
#include "3-system/process/raise.hpp"
#include "3-system/process/sigaction.hpp"
#include "3-system/process/sigaltstack.hpp"
#include "3-system/process/signal_info.hpp"
#include "3-system/process/signal_set.hpp"
#include "3-system/process/signals.hpp"
#include "3-system/process/sigprocmask.hpp"
#include "3-system/process/sigqueue.hpp"
#include "3-system/process/sigtimedwait.hpp"
#include "3-system/process/sigwaitinfo.hpp"

static char alternate[65536];
static volatile bool on_alternate = false;

static auto on_signal(int) -> void {
	const char* const here = static_cast<const char*>(__builtin_frame_address(0));
	on_alternate = here >= alternate && here < alternate + sizeof(alternate);
}

auto main(void) -> int {
	if (!ca::sys::sigaltstack(ca::span<char>{alternate}))
		return 1;
	const auto flags = ca::sys::sigaction_flags::restart | ca::sys::sigaction_flags::on_alternate_stack;
	if (!ca::sys::sigaction(ca::sys::signals::user2, on_signal, flags) || !ca::sys::raise(ca::sys::signals::user2) || !on_alternate)
		return 2;
	on_alternate = false;
	if (!ca::sys::sigaction(ca::sys::signals::user2, on_signal) || !ca::sys::raise(ca::sys::signals::user2) || on_alternate)
		return 3;
	if (!ca::sys::sigaltstack(ca::span<char>{}) || !ca::sys::sigaction(ca::sys::signals::user2, ca::sys::signal_disposition::default_action))
		return 4;
	char tiny[16];
	auto small = ca::sys::sigaltstack(ca::span<char>{tiny});
	if (small || small.error() != ca::errc::out_of_memory)
		return 5;

	ca::sys::signal_set set;
	set.add(ca::sys::signals::user1);
	auto previous = ca::sys::sigprocmask(ca::sys::mask_change::block, set);
	if (!previous)
		return 6;
	auto none = ca::sys::sigtimedwait(set, 1000000);
	if (none || none.error() != ca::errc::timed_out)
		return 7;
	auto poll = ca::sys::sigtimedwait(set, 0);
	if (poll || poll.error() != ca::errc::timed_out)
		return 8;

	if (!ca::sys::sigqueue(ca::sys::getpid(), ca::sys::signals::user1, -42))
		return 9;
	auto queued = ca::sys::sigwaitinfo(set);
	if (!queued || queued->signal() != ca::sys::signals::user1 || !queued->queued() || queued->value() != -42)
		return 10;
	if (queued->sender() != ca::sys::getpid() || queued->user() != ca::sys::getuid())
		return 11;

	if (!ca::sys::raise(ca::sys::signals::user1))
		return 12;
	auto raised = ca::sys::sigtimedwait(set, 1000000000);
	if (!raised || raised->signal() != ca::sys::signals::user1 || raised->queued() || raised->sender() != ca::sys::getpid())
		return 13;
	return ca::sys::sigprocmask(ca::sys::mask_change::replace, *previous) ? 0 : 14;
}
