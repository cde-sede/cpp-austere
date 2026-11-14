#ifndef ca_resources_io_impl_timer_node_hpp
#define ca_resources_io_impl_timer_node_hpp

#include "1-primitives/integers/i64.hpp"
#include "1-primitives/integers/u64.hpp"


// -- CA NAMESPACE -----------------------------------------------------------

namespace ca {


	// -- IO -----------------------------------------------------------------

	namespace io {


		class reactor;


		// -- TIMER NODE -----------------------------------------------------

		namespace impl {

			/* @DOC What a reactor keeps of a ca::io::timer: its deadline, its place in the reactor's
			   pairing heap (first child, next sibling, and the previous sibling or, for a first
			   child, the parent) and how to call the timer. A copy is not scheduled. */
			class timer_node {


				private:

					// -- friends --------------------------------------------

					friend class ca::io::reactor;


					// -- private types --------------------------------------

					using self = ca::io::impl::timer_node;


				protected:

					// -- protected types ------------------------------------

					using dispatch_type = void (*)(self&, ca::io::reactor&) noexcept;


				private:

					// -- private members ------------------------------------

					dispatch_type _dispatch;
					ca::io::reactor* _reactor;
					self* _child;
					self* _next;
					self* _previous;
					ca::i64 _deadline;
					ca::u64 _sequence;


				protected:

					// -- protected lifecycle --------------------------------

					constexpr explicit timer_node(const dispatch_type dispatch) noexcept
					: _dispatch{dispatch}, _reactor{nullptr}, _child{nullptr}, _next{nullptr}, _previous{nullptr}, _deadline{0}, _sequence{0U} {
					}

					constexpr timer_node(const self& other) noexcept
					: self{other._dispatch} {
					}


					// -- assignment operators -------------------------------

					constexpr auto operator=(const self&) noexcept -> self& {
						return *this;
					}


				public:

					// -- accessors ------------------------------------------

					/* @DOC Whether a reactor will call it. */
					[[nodiscard]] constexpr auto is_scheduled(void) const noexcept -> bool {
						return _reactor != nullptr;
					}

					/* @DOC When it is due, in sys::clock::monotonic nanoseconds; meaningful while
					   scheduled. */
					[[nodiscard]] constexpr auto deadline(void) const noexcept -> ca::i64 {
						return _deadline;
					}

			}; // class timer_node

		} // namespace impl

	} // namespace io

} // namespace ca

#endif // ca_resources_io_impl_timer_node_hpp
