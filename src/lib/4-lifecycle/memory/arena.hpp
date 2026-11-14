#ifndef ca_lifecycle_memory_arena_hpp
#define ca_lifecycle_memory_arena_hpp

#include "1-primitives/integers/usz.hpp"
#include "1-primitives/utility/exchange.hpp"
#include "2-vocabulary/result.hpp"
#include "4-lifecycle/memory/arena_allocator.hpp"
#include "4-lifecycle/memory/arena_scope.hpp"
#include "4-lifecycle/memory/impl/arena_state.hpp"


// -- CA NAMESPACE -----------------------------------------------------------

namespace ca {


	// -- ARENA --------------------------------------------------------------

	namespace impl {

		auto arena_release(ca::impl::arena_state* state) noexcept -> void;

		[[noreturn, gnu::noinline, gnu::cold]] auto allocator_of_an_empty_arena(void) noexcept -> void;

	} // namespace impl


	/* @DOC Owner of chunks of pages (page_allocator) carved by bumping a pointer, all released at
	   once: an allocator resource, handing out arena_allocator. A chunk holds chunk_size bytes,
	   or one larger block. push() opens a scope of temporaries (arena_scope). Moving an arena
	   keeps its allocators and scopes valid; cleanup() or reset() invalidates every block and
	   scope. Not thread-safe. */
	class arena final {


		private:

			// -- private types ----------------------------------------------

			using self = ca::arena;


			// -- private members --------------------------------------------

			ca::impl::arena_state* _state;


		public:

			// -- public constants -------------------------------------------

			static constexpr ca::usz CHUNK_SIZE = 65536U;


			// -- public lifecycle -------------------------------------------

			/* @DOC An empty arena, owning nothing; make() builds a usable one. */
			constexpr arena(void) noexcept
			: _state{nullptr} {
			}

			arena(const self&) = delete;

			constexpr arena(self&& other) noexcept
			: _state{ca::exchange(other._state, nullptr)} {
			}

			// Failsafe for a skipped cleanup().
			// ~arena(void) noexcept {
			// 	ca::impl::arena_release(_state);
			// }

			/* @DOC An arena with its first chunk, chunk_size bytes rounded up to pages. */
			[[nodiscard]] static auto make(ca::usz chunk_size = self::CHUNK_SIZE) noexcept -> ca::result<self>;


			// -- assignment operators ---------------------------------------

			auto operator=(const self&) -> self& = delete;

			/* @DOC Takes other's chunks; the previous ones are released. */
			auto operator=(self&& other) noexcept -> self& {

				if (this == &other)
					return *this;
				ca::impl::arena_release(_state);
				_state = ca::exchange(other._state, nullptr);
				return *this;
			}


			// -- accessors --------------------------------------------------

			/* @DOC Whether make() built it and cleanup() has not run. */
			[[nodiscard]] constexpr auto holds(void) const noexcept -> bool {
				return _state != nullptr;
			}

			/* @DOC An allocator over this arena. Traps on an empty arena. */
			[[nodiscard]] auto allocator(void) const noexcept -> ca::arena_allocator {

				if (_state == nullptr) [[unlikely]]
					ca::impl::allocator_of_an_empty_arena();
				return ca::arena_allocator{&_state->_lanes[0]};
			}

			/* @DOC A scope of temporaries over this arena, released by its pop(); its results are
			   blocks of the arena, as from allocator(). Traps on an empty arena. */
			[[nodiscard]] auto push(void) const noexcept -> ca::arena_scope {

				if (_state == nullptr) [[unlikely]]
					ca::impl::allocator_of_an_empty_arena();
				return ca::arena_scope{&_state->_lanes[1]};
			}


			// -- methods ----------------------------------------------------

			/* @DOC Releases every chunk but the first, and empties it: every block and scope is
			   gone, the allocators stay valid. */
			auto reset(void) noexcept -> void;

			/* @DOC Releases every chunk; the arena is empty after. */
			[[nodiscard]] auto cleanup(void) noexcept -> ca::result<void> {

				ca::impl::arena_release(ca::exchange(_state, nullptr));
				return ca::result<void>{};
			}

	}; // class arena

} // namespace ca

#endif // ca_lifecycle_memory_arena_hpp
