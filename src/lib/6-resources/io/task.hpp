#ifndef ca_resources_io_task_hpp
#define ca_resources_io_task_hpp

#include "6-resources/io/impl/task_node.hpp"
#include "6-resources/io/reactor.hpp"


// -- CA NAMESPACE -----------------------------------------------------------

namespace ca {


	// -- IO -----------------------------------------------------------------

	namespace io {


		// -- TASK -----------------------------------------------------------

		/* @DOC Base of work handed to a reactor from any thread, Self deriving from task<Self>:
		   reactor::post queues it and the reactor's thread calls Self's public
		   on_posted(ca::io::reactor&), no longer posted by then, so it may be posted again. The
		   way other threads reach a reactor's watchers and timers. A copy of a task is not posted;
		   a posted one stays alive until it has run. */
		template <typename Self>
		class task : public ca::io::impl::task_node { // final: ignore


			private:

				// -- private types ------------------------------------------

				using self = ca::io::task<Self>;


				// -- private methods ----------------------------------------

				static auto _dispatch_to(ca::io::impl::task_node& node, ca::io::reactor& r) noexcept -> void {
					static_cast<Self&>(static_cast<self&>(node)).on_posted(r);
				}


			public:

				// -- public lifecycle ---------------------------------------

				constexpr task(void) noexcept
				: ca::io::impl::task_node{&self::_dispatch_to} {
				}

				constexpr task(const self&) noexcept = default;


				// -- assignment operators -----------------------------------

				constexpr auto operator=(const self&) noexcept -> self& = default;

		}; // class task

	} // namespace io

} // namespace ca

#endif // ca_resources_io_task_hpp
