#ifndef ca_lifecycle_impl_run_context_hpp
#define ca_lifecycle_impl_run_context_hpp

#include "1-primitives/config/compiler.hpp"
#include "1-primitives/config/sanitizer.hpp"
#include "1-primitives/debug/recovery.hpp"
#include "1-primitives/debug/verdict.hpp"
#include "1-primitives/traits/is_constructible.hpp"
#include "1-primitives/traits/is_copy_constructible.hpp"
#include "1-primitives/traits/is_same.hpp"
#include "1-primitives/traits/is_void.hpp"
#include "1-primitives/utility/move.hpp"
#include "1-primitives/utility/type_key.hpp"
#include "2-vocabulary/error.hpp"
#include "2-vocabulary/failure.hpp"
#include "2-vocabulary/optional.hpp"
#include "2-vocabulary/result.hpp"
#include "2-vocabulary/sv/str.hpp"
#include "3-system/error/errc.hpp"
#include "3-system/trace/attach.hpp"
#include "3-system/trace/backtrace.hpp"
#include "3-system/trace/context.hpp"
#include "3-system/trace/fail.hpp"
#include "4-lifecycle/outcome.hpp"
#include "4-lifecycle/trapped.hpp"


// -- CA NAMESPACE -----------------------------------------------------------

namespace ca {


	// -- RUN CONTEXT --------------------------------------------------------

	namespace impl {

		/* @DOC Whether a context can fail with E: E is ca::error, or made from one by E::of(error)
		   (ca::report) or a constructor. */
		template <typename E>
		concept is_context_error = ca::is_same<E, ca::error>
		                        || requires (const ca::error error) { { E::of(error) } -> ca::is_same<E>; }
		                        || ca::is_constructible<E, ca::error>;

		template <typename E>
		[[nodiscard]] auto context_error_from(const ca::error error) noexcept -> E {

			if constexpr (ca::is_same<E, ca::error>)
				return error;
			else if constexpr (requires { { E::of(error) } -> ca::is_same<E>; })
				return E::of(error);
			else
				return E{error};
		}

		/* @DOC Sets point, makes it the thread's innermost, runs body; true when a trap came back
		   to point. Opaque to gcc's interprocedural analysis: when body never returns normally, gcc
		   would otherwise take the landing path for the only exit and assume body's stores never
		   reach the caller. Not instrumented by ASan: its frame cleanup, run again when the trap
		   lands back here, writes the shadow of frames the jump already left. */
		template <typename F>
#if ca_compiler(gcc)
		[[gnu::noipa, ca_no_sanitize(address)]]
#else
		[[gnu::noinline, ca_no_sanitize(address)]]
#endif
		auto enter_recovery(ca::impl::recovery& point, F& body) noexcept -> bool {

			if (__builtin_setjmp(point._buffer) != 0)
				return true;
			ca::impl::current_recovery = &point;
			body();
			return false;
		}

		/* @DOC The ca::error of a trap without an E: the error of the failed result accessed, its
		   trace extended with the reason, else not_recoverable with a new trace; the frame where
		   the trap was (the raise call), or with no location when unknown. Recorded on info the
		   first time, so the contexts a trap passes and the one taking it share one frame, with
		   the trap's backtrace attached; the first time is always while the trapping frames are
		   alive (a context passed, or the one taking it, before the trap unwinds). */
		[[gnu::cold, gnu::noinline]] inline auto traced_error(ca::impl::trap_info& info) noexcept -> ca::error {

			if (!info._traced) {
				const ca::sv::str reason = info._reason == nullptr ? ca::sv::str{} : ca::sv::str{info._reason};
				const ca::error error = info._code != 0
				                      ? ca::trace::context(ca::error{info._code, info._trace}, reason, info._where).error()
				                      : ca::trace::fail(ca::errc::not_recoverable, reason, info._where).error();
				ca::trace::attach(error, ca::trace::backtrace::walk(info._frame));
				  info._code = error.code();
				 info._trace = error.trace();
				info._traced = true;
			}
			return ca::error{info._code, info._trace};
		}

		/* @DOC recovery::_take for a context failing with E, before the trap unwinds to it: takes
		   the trap's error when an E, copied, or moved when E cannot be copied (its trace, if any,
		   gets the trap's backtrace); else records the trap in the thread's trace
		   (traced_error). */
		template <typename E>
		auto take_context(ca::impl::recovery* const point, ca::impl::trap_info& info) noexcept -> void {

			if (info._payload == nullptr || info._type != ca::impl::type_key<E>) {
				static_cast<void>(ca::impl::traced_error(info));
				return;
			}
			ca::optional<E>& out = *static_cast<ca::optional<E>*>(point->_sink);
			if constexpr (ca::is_copy_constructible<E>)
				out.emplace(*static_cast<const E*>(info._payload));
			else
				out.emplace(ca::move(*static_cast<E*>(info._payload)));
			point->_accepted = true;
			if (info._trace != 0U)
				ca::trace::attach(ca::error{info._code, info._trace}, ca::trace::backtrace::walk(info._frame));
		}

		/* @DOC run_context's Handles for a context without handles: it takes every trap. */
		struct takes_every_trap final {

				using self = ca::impl::takes_every_trap;

		}; // struct takes_every_trap

		template <typename Handles, typename Leave>
		struct context_callbacks final {

				using self = ca::impl::context_callbacks<Handles, Leave>;

				Handles* _handles;
				Leave* _leave;

		}; // struct context_callbacks

		/* @DOC recovery::_handles: the context's handles on the trap. */
		template <typename Handles, typename Leave>
		auto decide_context(void* const context, const ca::impl::trap_info& info) noexcept -> ca::verdict {
			return (*static_cast<ca::impl::context_callbacks<Handles, Leave>*>(context)->_handles)(ca::trapped{info});
		}

		/* @DOC recovery::_unwind: the context's leave for a trap passing it, outcome passed, with
		   the trap's E as it is, else its ca::error made an E (cleaned up after leave, nobody
		   owning it). */
		template <typename E, typename Handles, typename Leave>
		auto unwind_context(void* const context, ca::impl::trap_info& info) noexcept -> void {

			Leave& leave = *static_cast<ca::impl::context_callbacks<Handles, Leave>*>(context)->_leave;
			if (info._payload != nullptr && info._type == ca::impl::type_key<E>) {
				leave(static_cast<const E*>(info._payload), ca::outcome::passed);
				return;
			}
			E error = ca::impl::context_error_from<E>(ca::impl::traced_error(info));
			leave(&error, ca::outcome::passed);
			if constexpr (requires { error.cleanup(); })
				static_cast<void>(error.cleanup());
		}

		/* @DOC Calls leave(error, outcome) on state when it asks for the outcome, else leave(error). */
		template <typename Self, typename E>
		auto leave_state(Self& state, const E* const error, const ca::outcome outcome) noexcept -> void {

			if constexpr (requires { state.leave(error, outcome); })
				state.leave(error, outcome);
			else
				state.leave(error);
		}

		/* @DOC Runs enter() inside a new context, then leave(error, outcome), error nullptr on
		   success. The context fails with what enter returns when it returns a failed
		   result<void, E>, or with the error of a trap reached inside it that handles takes
		   (every trap, for takes_every_trap): an E as it was, else a ca::error made an E. A trap it
		   passes runs leave with outcome passed, during the trap, and run_context never returns. */
		template <typename E, typename Handles, typename Enter, typename Leave>
		[[nodiscard]] auto run_context(Handles& handles, Enter& enter, Leave& leave) noexcept -> ca::result<void, E> {

			static_assert(ca::impl::is_context_error<E>, "ca::context_manager: the error type must be made from a ca::error");

			ca::optional<E> error{};
			ca::impl::context_callbacks<Handles, Leave> callbacks{&handles, &leave};
			ca::impl::recovery point{};
			 point._parent = ca::impl::current_recovery;
			point._context = &callbacks;
			if constexpr (!ca::is_same<Handles, ca::impl::takes_every_trap>) {
				point._handles = &ca::impl::decide_context<Handles, Leave>;
				 point._unwind = &ca::impl::unwind_context<E, Handles, Leave>;
			}
			   point._sink = &error;
			   point._take = &ca::impl::take_context<E>;
			auto body = [&](void) noexcept -> void {
				if constexpr (ca::is_void<decltype(enter())>)
					enter();
				else {
					auto entered = enter();
					if (!entered)
						error.emplace(ca::move(entered).error());
				}
			};

			const bool trapped = ca::impl::enter_recovery(point, body);
			ca::impl::current_recovery = point._parent;
			if (trapped && !point._accepted) [[unlikely]]
				error.emplace(ca::impl::context_error_from<E>(ca::impl::traced_error(point._trap)));
			if (!error.holds()) {
				leave(static_cast<const E*>(nullptr), ca::outcome::succeeded);
				return {};
			}
			leave(&*error, ca::outcome::taken);
			return ca::failure{*ca::move(error)};
		}

	} // namespace impl

} // namespace ca

#endif // ca_lifecycle_impl_run_context_hpp
