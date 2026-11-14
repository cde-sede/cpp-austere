#ifndef ca_resources_io_watcher_hpp
#define ca_resources_io_watcher_hpp

#include "1-primitives/integers/u32.hpp"
#include "3-system/io/epoll_events.hpp"
#include "6-resources/io/impl/watch_node.hpp"
#include "6-resources/io/interest.hpp"
#include "6-resources/io/reactor.hpp"


// -- CA NAMESPACE -----------------------------------------------------------

namespace ca {


	// -- IO -----------------------------------------------------------------

	namespace io {


		// -- WATCHER --------------------------------------------------------

		namespace impl {

			template <typename Self>
			concept has_on_readable = requires (Self& self, ca::io::reactor& r) { self.on_readable(r); };

			template <typename Self>
			concept has_on_writable = requires (Self& self, ca::io::reactor& r) { self.on_writable(r); };

			template <typename Self>
			concept has_on_hangup = requires (Self& self, ca::io::reactor& r) { self.on_hangup(r); };

		} // namespace impl


		/* @DOC Base of a descriptor handler, Self deriving from watcher<Self>; reactor::watch
		   registers it. Self has public on_readable(ca::io::reactor&), on_writable(reactor&) or
		   both, called when the descriptor is ready for what it is watched for, and optionally
		   on_hangup(reactor&), called instead when the descriptor reports an error or a hangup;
		   without it those go to every handler Self has, whatever the interest, as readiness (the
		   next read returns 0 or the error), so a level-triggered failure never spins unhandled.
		   When both are ready, on_readable runs first, and on_writable only if
		   on_readable left it watched. A copy of a watcher is not watched; unwatch one before
		   destroying it. */
		template <typename Self>
		class watcher : public ca::io::impl::watch_node { // final: ignore


			private:

				// -- private types ------------------------------------------

				using self = ca::io::watcher<Self>;


				// -- private methods ----------------------------------------

				static auto _dispatch_to(ca::io::impl::watch_node& node, ca::io::reactor& r, const ca::u32 events) noexcept -> void {

					static_assert(ca::io::impl::has_on_readable<Self> || ca::io::impl::has_on_writable<Self>, "a watcher has on_readable(reactor&), on_writable(reactor&) or both");
					Self& handler = static_cast<Self&>(static_cast<self&>(node));
					constexpr ca::u32 failed = ca::sys::epoll_events::error | ca::sys::epoll_events::hangup;
					ca::u32 ready = events;
					if ((events & failed) != 0U) {
						if constexpr (ca::io::impl::has_on_hangup<Self>) {
							handler.on_hangup(r);
							return;
						}
						ready |= ca::io::interest::readable | ca::io::interest::writable;
					}
					if constexpr (ca::io::impl::has_on_readable<Self>) {
						if ((ready & ca::io::interest::readable) != 0U) {
							handler.on_readable(r);
							if (!r._is_current(node))
								return;
						}
					}
					if constexpr (ca::io::impl::has_on_writable<Self>) {
						if ((ready & ca::io::interest::writable) != 0U)
							handler.on_writable(r);
					}
				}


			public:

				// -- public lifecycle ---------------------------------------

				constexpr watcher(void) noexcept
				: ca::io::impl::watch_node{&self::_dispatch_to} {
				}

				constexpr watcher(const self&) noexcept = default;


				// -- assignment operators -----------------------------------

				constexpr auto operator=(const self&) noexcept -> self& = default;

		}; // class watcher

	} // namespace io

} // namespace ca

#endif // ca_resources_io_watcher_hpp
