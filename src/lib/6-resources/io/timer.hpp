#ifndef ca_resources_io_timer_hpp
#define ca_resources_io_timer_hpp

#include "6-resources/io/impl/timer_node.hpp"
#include "6-resources/io/reactor.hpp"


// -- CA NAMESPACE -----------------------------------------------------------

namespace ca {


	// -- IO -----------------------------------------------------------------

	namespace io {


		// -- TIMER ----------------------------------------------------------

		/* @DOC Base of a timer, Self deriving from timer<Self>: reactor::schedule_at and
		   schedule_after arm it, and the reactor calls Self's public on_timer(ca::io::reactor&)
		   once when it is due, unscheduled by then, so on_timer may schedule it again (a periodic
		   timer). A copy of a timer is not scheduled; cancel one before destroying it. */
		template <typename Self>
		class timer : public ca::io::impl::timer_node { // final: ignore


			private:

				// -- private types ------------------------------------------

				using self = ca::io::timer<Self>;


				// -- private methods ----------------------------------------

				static auto _dispatch_to(ca::io::impl::timer_node& node, ca::io::reactor& r) noexcept -> void {
					static_cast<Self&>(static_cast<self&>(node)).on_timer(r);
				}


			public:

				// -- public lifecycle ---------------------------------------

				constexpr timer(void) noexcept
				: ca::io::impl::timer_node{&self::_dispatch_to} {
				}

				constexpr timer(const self&) noexcept = default;


				// -- assignment operators -----------------------------------

				constexpr auto operator=(const self&) noexcept -> self& = default;

		}; // class timer

	} // namespace io

} // namespace ca

#endif // ca_resources_io_timer_hpp
