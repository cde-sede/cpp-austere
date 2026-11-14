#ifndef ca_lifecycle_memory_arena_scope_hpp
#define ca_lifecycle_memory_arena_scope_hpp

#include "1-primitives/integers/usz.hpp"
#include "2-vocabulary/optional.hpp"
#include "2-vocabulary/result.hpp"
#include "4-lifecycle/memory/allocation.hpp"
#include "4-lifecycle/memory/arena_allocator.hpp"
#include "4-lifecycle/memory/impl/arena_state.hpp"


// -- CA NAMESPACE -----------------------------------------------------------

namespace ca {


	// -- ARENA SCOPE --------------------------------------------------------

	namespace impl {

		auto arena_pop(ca::impl::arena_lane* lane, char* mark) noexcept -> void;

		[[noreturn, gnu::noinline, gnu::cold]] auto pop_of_a_stale_scope(void) noexcept -> void;

	} // namespace impl


	/* @DOC Stateful allocator of temporaries, from arena::push() or push() of an enclosing scope;
	   two pointers wide. pop() releases every block it gave since the push. What must outlive
	   the pop is allocated through result(): blocks of the enclosing scope (or of the arena),
	   which the pop leaves in place, so nothing moves and structures pointing into themselves
	   survive. Scopes alternate between two lanes of the arena: a scope's temporaries share a
	   lane with its parent's results, and its results with its parent's temporaries. Hence the
	   rule: allocate only through the innermost scope, or through result() of it; allocating
	   through an enclosing one while a nested scope is open puts the block in memory the nested
	   pop releases. Copies are interchangeable, pop() through any of them; after it, neither
	   the scope nor its copies are used again, nor its blocks deallocated, and popping the
	   enclosing scope pops it too. A pop whose mark is gone traps. Not thread-safe. */
	class arena_scope final {


		private:

			// -- private types ----------------------------------------------

			using self = ca::arena_scope;


			// -- private members --------------------------------------------

			ca::impl::arena_lane* _lane;
			char*                 _mark;


		public:

			// -- public lifecycle -------------------------------------------

			/* @DOC A scope marking lane where it stands; arena::push() builds it. */
			constexpr explicit arena_scope(ca::impl::arena_lane* const lane) noexcept
			: _lane{lane}, _mark{lane->_top} {
			}


			// -- accessors --------------------------------------------------

			/* @DOC An allocator whose blocks survive pop(): those of the enclosing scope. */
			[[nodiscard]] constexpr auto result(void) const noexcept -> ca::arena_allocator {
				return ca::arena_allocator{_lane->_other};
			}


			// -- methods ----------------------------------------------------

			[[nodiscard, gnu::always_inline]] auto allocate(const ca::usz size, const ca::usz alignment) const noexcept -> ca::result<ca::allocation> {
				return ca::arena_allocator{_lane}.allocate(size, alignment);
			}

			/* @DOC Gives the room back when block is the newest one; otherwise nothing. */
			[[gnu::always_inline]] auto deallocate(const ca::allocation block, const ca::usz alignment) const noexcept -> void {
				ca::arena_allocator{_lane}.deallocate(block, alignment);
			}

			/* @DOC The newest block grows or shrinks while its chunk has room; any block shrinks by
			   keeping its size. */
			[[nodiscard]] auto expand(const ca::allocation block, const ca::usz size, const ca::usz alignment) const noexcept -> ca::optional<ca::allocation> {
				return ca::arena_allocator{_lane}.expand(block, size, alignment);
			}

			/* @DOC A scope nested in this one: its results are temporaries of this one. */
			[[nodiscard]] auto push(void) const noexcept -> self {
				return self{_lane->_other};
			}

			/* @DOC Releases every block of the scope, the chunks they took but one kept for the
			   next growth. Results stay. */
			auto pop(void) const noexcept -> void {
				ca::impl::arena_pop(_lane, _mark);
			}

	}; // class arena_scope

} // namespace ca

#endif // ca_lifecycle_memory_arena_scope_hpp
