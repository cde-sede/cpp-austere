#ifndef ca_system_io_epoll_event_hpp
#define ca_system_io_epoll_event_hpp

#include "1-primitives/config/architecture.hpp"
#include "1-primitives/integers/u32.hpp"
#include "1-primitives/integers/u64.hpp"


// -- CA NAMESPACE -----------------------------------------------------------

namespace ca {


	// -- SYS ----------------------------------------------------------------

	namespace sys {


		// -- EPOLL EVENT ----------------------------------------------------

		namespace impl {

			struct epoll_layout;

		} // namespace impl


		/* @DOC What an epoll registration asks for (sys::epoll_events) and the word it hands back
		   with each report (data: an index, a pointer as an integer). Same layout as struct
		   epoll_event, packed on x86-64 as the kernel has it, so arrays pass to the kernel
		   uncopied. */
#if ca_architecture(x86_64)
		class [[gnu::packed]] epoll_event final {
#else
		class epoll_event final {
#endif


			private:

				// -- friends ------------------------------------------------

				friend struct ca::sys::impl::epoll_layout;


				// -- private types ------------------------------------------

				using self = ca::sys::epoll_event;


				// -- private members ----------------------------------------

				ca::u32 _events;
				ca::u64 _data;


			public:

				// -- public lifecycle ---------------------------------------

				constexpr epoll_event(void) noexcept
				: _events{0U}, _data{0U} {
				}

				constexpr epoll_event(const ca::u32 events, const ca::u64 data) noexcept
				: _events{events}, _data{data} {
				}


				// -- accessors ----------------------------------------------

				/* @DOC Events requested, or reported once epoll_wait filled it. */
				[[nodiscard]] constexpr auto events(void) const noexcept -> ca::u32 {
					return _events;
				}

				[[nodiscard]] constexpr auto data(void) const noexcept -> ca::u64 {
					return _data;
				}

				/* @DOC Whether any of events is set. */
				[[nodiscard]] constexpr auto has(const ca::u32 events) const noexcept -> bool {
					return (_events & events) != 0U;
				}

		}; // class epoll_event

	} // namespace sys

} // namespace ca

#endif // ca_system_io_epoll_event_hpp
