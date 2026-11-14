#ifndef ca_resources_io_interest_hpp
#define ca_resources_io_interest_hpp

#include "1-primitives/integers/u32.hpp"


// -- CA NAMESPACE -----------------------------------------------------------

namespace ca {


	// -- IO -----------------------------------------------------------------

	namespace io {


		// -- INTEREST -------------------------------------------------------

		/* @DOC What a reactor watches a descriptor for, combined with |. Level-triggered by default:
		   a watcher is called while the descriptor stays ready, so one read per call is enough.
		   edge: called once per change instead, so each call must read (or write) until
		   errc::try_again; it saves the syscalls that toggling writable costs. Errors and hangups
		   are reported whatever is asked, none included. */
		namespace interest {

			inline constexpr ca::u32 none     = 0x0U;
			inline constexpr ca::u32 readable = 0x1U;
			inline constexpr ca::u32 writable = 0x4U;
			inline constexpr ca::u32 edge     = 1U << 31U;

		} // namespace interest

	} // namespace io

} // namespace ca

#endif // ca_resources_io_interest_hpp
