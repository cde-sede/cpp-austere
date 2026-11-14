#ifndef ca_lifecycle_memory_arena_allocator_hpp
#define ca_lifecycle_memory_arena_allocator_hpp

#include "1-primitives/integers/usz.hpp"
#include "1-primitives/memory/poison.hpp"
#include "2-vocabulary/nullopt.hpp"
#include "2-vocabulary/optional.hpp"
#include "2-vocabulary/result.hpp"
#include "4-lifecycle/memory/allocation.hpp"
#include "4-lifecycle/memory/impl/arena_state.hpp"


// -- CA NAMESPACE -----------------------------------------------------------

namespace ca {


	// -- ARENA ALLOCATOR ----------------------------------------------------

	namespace impl {

		[[nodiscard, gnu::noinline]] auto arena_grow(ca::impl::arena_lane* lane, ca::usz size, ca::usz alignment) noexcept -> ca::result<ca::allocation>;

	} // namespace impl


	/* @DOC Stateful allocator handing out blocks of an arena, one pointer wide; from
	   arena::allocator() or arena_scope::result(). Copies share the arena, which must outlive
	   them. Allocation bumps a pointer; deallocate and expand act on the newest block only, and
	   are no-ops on others. Not thread-safe. Under ca_memory_poisoning, the room of a chunk past
	   its newest block, alignment padding and what a pop or a deallocate gave back are poisoned
	   for ASan. */
	class arena_allocator final {


		private:

			// -- private types ----------------------------------------------

			using self = ca::arena_allocator;


			// -- private members --------------------------------------------

			ca::impl::arena_lane* _lane;


		public:

			// -- public lifecycle -------------------------------------------

			/* @DOC Over a lane of an arena; arena::allocator() builds it. */
			constexpr explicit arena_allocator(ca::impl::arena_lane* const lane) noexcept
			: _lane{lane} {
			}


			// -- methods ----------------------------------------------------

			[[nodiscard, gnu::always_inline]] auto allocate(const ca::usz size, const ca::usz alignment) const noexcept -> ca::result<ca::allocation> {

				const ca::usz end = reinterpret_cast<ca::usz>(_lane->_end);
				const ca::usz start = (reinterpret_cast<ca::usz>(_lane->_top) + alignment - 1U) & ~(alignment - 1U);
				if (start <= end && size <= end - start) [[likely]] {
					_lane->_top = reinterpret_cast<char*>(start + size);
					ca::impl::unpoison_bytes(reinterpret_cast<void*>(start), size);
					return ca::allocation{reinterpret_cast<void*>(start), size};
				}
				return ca::impl::arena_grow(_lane, size, alignment);
			}

			/* @DOC Gives the room back when block is the newest one; otherwise nothing. */
			[[gnu::always_inline]] auto deallocate(const ca::allocation block, const ca::usz) const noexcept -> void {
				if (static_cast<char*>(block.data()) + block.size() == _lane->_top) {
					_lane->_top = static_cast<char*>(block.data());
					ca::impl::poison_bytes(block.data(), block.size());
				}
			}

			/* @DOC The newest block grows or shrinks while its chunk has room; any block shrinks by
			   keeping its size. */
			[[nodiscard]] auto expand(const ca::allocation block, const ca::usz size, const ca::usz) const noexcept -> ca::optional<ca::allocation> {

				char* const data = static_cast<char*>(block.data());
				if (data + block.size() == _lane->_top && size <= static_cast<ca::usz>(_lane->_end - data)) {
					_lane->_top = data + size;
					ca::impl::poison_bytes(data, block.size());
					ca::impl::unpoison_bytes(data, size);
					return ca::allocation{data, size};
				}
				if (size <= block.size())
					return block;
				return ca::nullopt;
			}

	}; // class arena_allocator

} // namespace ca

#endif // ca_lifecycle_memory_arena_allocator_hpp
