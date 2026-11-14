#ifndef ca_resources_io_impl_watch_node_hpp
#define ca_resources_io_impl_watch_node_hpp

#include "1-primitives/integers/u32.hpp"


// -- CA NAMESPACE -----------------------------------------------------------

namespace ca {


	// -- IO -----------------------------------------------------------------

	namespace io {


		class reactor;


		// -- WATCH NODE -----------------------------------------------------

		namespace impl {

			/* @DOC What a reactor keeps of a ca::io::watcher: the descriptor, the interest and how to
			   call the watcher. Its address is the word epoll hands back, so it stays put while
			   watched. A copy is not watched: registrations belong to the place, not the value. */
			class watch_node {


				private:

					// -- friends --------------------------------------------

					friend class ca::io::reactor;


					// -- private types --------------------------------------

					using self = ca::io::impl::watch_node;


				protected:

					// -- protected types ------------------------------------

					using dispatch_type = void (*)(self&, ca::io::reactor&, ca::u32) noexcept;


				private:

					// -- private members ------------------------------------

					dispatch_type _dispatch;
					ca::io::reactor* _reactor;
					int _fd;
					ca::u32 _interest;


				protected:

					// -- protected lifecycle --------------------------------

					constexpr explicit watch_node(const dispatch_type dispatch) noexcept
					: _dispatch{dispatch}, _reactor{nullptr}, _fd{-1}, _interest{0U} {
					}

					constexpr watch_node(const self& other) noexcept
					: _dispatch{other._dispatch}, _reactor{nullptr}, _fd{-1}, _interest{0U} {
					}


					// -- assignment operators -------------------------------

					constexpr auto operator=(const self&) noexcept -> self& {
						return *this;
					}


				public:

					// -- accessors ------------------------------------------

					/* @DOC Whether a reactor watches it. */
					[[nodiscard]] constexpr auto is_watched(void) const noexcept -> bool {
						return _reactor != nullptr;
					}

					/* @DOC The descriptor watched, -1 when not watched. */
					[[nodiscard]] constexpr auto fd(void) const noexcept -> int {
						return _fd;
					}

					/* @DOC The ca::io::interest bits it is watched for. */
					[[nodiscard]] constexpr auto interest(void) const noexcept -> ca::u32 {
						return _interest;
					}

			}; // class watch_node

		} // namespace impl

	} // namespace io

} // namespace ca

#endif // ca_resources_io_impl_watch_node_hpp
