#ifndef ca_system_syscall_auxiliary_hpp
#define ca_system_syscall_auxiliary_hpp

#include "1-primitives/integers/u64.hpp"
#include "2-vocabulary/optional.hpp"
#include "3-system/elf/auxiliary_entry.hpp"


// -- CA NAMESPACE -----------------------------------------------------------

namespace ca {


	// -- SYSCALL ------------------------------------------------------------

	namespace syscall {


		// -- AUXILIARY ------------------------------------------------------

		/* @DOC Value of entry type (elf::auxiliary_types) of the auxiliary vector the kernel handed
		   the process. Read from the vector itself when libca_runtime's _start kept it
		   (impl::auxiliary_vector), else from /proc/self/auxv at each call: for one-time lookups
		   (the vDSO). None when the vector has no such entry or cannot be read (no /proc), which
		   a caller cannot tell apart: one deciding something security-related (secure) treats
		   none as the restrictive answer. Linux x86-64; none elsewhere. */
		[[nodiscard]] auto auxiliary(ca::u64 type) noexcept -> ca::optional<ca::u64>;

		namespace impl {

			/* @DOC The vector on the initial stack, past the environment, set by libca_runtime's
			   _start before anything runs; null in a program started by libc. */
			extern constinit const ca::elf::auxiliary_entry* auxiliary_vector;

		} // namespace impl

	} // namespace syscall

} // namespace ca

#endif // ca_system_syscall_auxiliary_hpp
