#ifndef ca_system_memory_shared_segment_hpp
#define ca_system_memory_shared_segment_hpp

#include "1-primitives/integers/byte.hpp"
#include "1-primitives/integers/usz.hpp"
#include "2-vocabulary/result.hpp"
#include "2-vocabulary/span.hpp"


// -- CA NAMESPACE -----------------------------------------------------------

namespace ca {


	// -- SYS ----------------------------------------------------------------

	namespace sys {


		// -- SHARED SEGMENT -------------------------------------------------

		/* @DOC A new System V shared memory segment of size bytes, private (IPC_PRIVATE), readable
		   and writable by the owner only; returns its id. It outlives the process until
		   shm_remove: mark it removed once every party attached it, and the kernel frees it when
		   the last one detaches. What X11's MIT-SHM extension shares with the X server. */
		[[nodiscard]] auto shm_create(ca::usz size) noexcept -> ca::result<int>;

		/* @DOC Maps segment id, of size bytes (as created), into the process, read and write. */
		[[nodiscard]] auto shm_attach(int id, ca::usz size) noexcept -> ca::result<ca::span<ca::byte>>;

		/* @DOC Unmaps a segment mapping, as returned by shm_attach. */
		[[nodiscard]] auto shm_detach(ca::span<ca::byte> mapping) noexcept -> ca::result<void>;

		/* @DOC Marks segment id for removal (IPC_RMID): freed once no one has it attached, and no
		   longer attachable by id on Linux once removed and unattached. */
		[[nodiscard]] auto shm_remove(int id) noexcept -> ca::result<void>;

	} // namespace sys

} // namespace ca

#endif // ca_system_memory_shared_segment_hpp
