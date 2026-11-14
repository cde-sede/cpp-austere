#ifndef ca_resources_io_impl_task_node_hpp
#define ca_resources_io_impl_task_node_hpp

#include "1-primitives/atomic/atomic.hpp"


// -- CA NAMESPACE -----------------------------------------------------------

namespace ca {


	// -- IO -----------------------------------------------------------------

	namespace io {


		class reactor;


		// -- TASK NODE ------------------------------------------------------

		namespace impl {

			/* @DOC What a reactor keeps of a ca::io::task: its link in the reactor's lock-free stack of
			   posted tasks, whether it is in it, and how to call the task. A copy is not posted. */
			class task_node {


				private:

					// -- friends --------------------------------------------

					friend class ca::io::reactor;


					// -- private types --------------------------------------

					using self = ca::io::impl::task_node;


				protected:

					// -- protected types ------------------------------------

					using dispatch_type = void (*)(self&, ca::io::reactor&) noexcept;


				private:

					// -- private members ------------------------------------

					dispatch_type _dispatch;
					self* _next;
					ca::atomic<bool> _queued;


				protected:

					// -- protected lifecycle --------------------------------

					constexpr explicit task_node(const dispatch_type dispatch) noexcept
					: _dispatch{dispatch}, _next{nullptr}, _queued{false} {
					}

					constexpr task_node(const self& other) noexcept
					: self{other._dispatch} {
					}


					// -- assignment operators -------------------------------

					constexpr auto operator=(const self&) noexcept -> self& {
						return *this;
					}


				public:

					// -- accessors ------------------------------------------

					/* @DOC Whether it is posted and has not started running. Any thread may ask; the
					   answer may be stale by the time it is used. */
					[[nodiscard]] auto is_posted(void) const noexcept -> bool {
						return _queued.load<ca::memory_order::acquire>();
					}

			}; // class task_node

		} // namespace impl

	} // namespace io

} // namespace ca

#endif // ca_resources_io_impl_task_node_hpp
