/* @DOC The heap engine behind ca::heap, for heaps that get their pages, their notion of "current"
   and their size classes from elsewhere (a kernel: frames, one heap per CPU, interrupts off).

   ca::basic_heap<Pages, G, Binding>:
   - Pages, a stateless allocator: spans (G::SPAN_SIZE bytes aligned to G::SPAN_SIZE), blocks
     past G::LARGEST_CLASS, and chunks of heap states. Its expand, reallocate and allocate_zeroed
     are used when it has them. With G::STATISTICS, a static or const usable_size(size,
     alignment) gives the bytes a large block of that size holds, the same for the size asked
     and the size returned; without one, sizes round up to Pages::ALIGNMENT.
   - G, a geometry (ca::is_heap_geometry).
   - Binding, a class template instantiated on the heap (Binding<basic_heap<...>>), with static
     members: current() returning the calling context's state_type* (the heap's empty_state()
     until adopt), always inlined; pin() and unpin(token), around every use of a heap's own
     spans, so that nothing else runs on that heap meanwhile (nothing for a thread's heap;
     interrupts off for a CPU's); adopt(state_type*), called once a context got its heap, outside
     any lock of the heap, which makes it current. A context whose heap goes away for good
     (a thread exiting) passes it to retire(), after pointing current() back to empty_state().

   The class holds the fast paths; the rest is in impl/heap_engine.hpp, which only the translation
   unit instantiating a heap includes, then names it: `template class ca::basic_heap<...>;`
   (heap.cpp does it for ca::heap). Two instantiations share nothing. */

#ifndef ca_lifecycle_memory_basic_heap_hpp
#define ca_lifecycle_memory_basic_heap_hpp

#include "1-primitives/config/debug.hpp"
#include "1-primitives/config/sanitizer.hpp"
#include "1-primitives/integers/u32.hpp"
#include "1-primitives/integers/u64.hpp"
#include "1-primitives/integers/usz.hpp"
#include "2-vocabulary/optional.hpp"
#include "2-vocabulary/result.hpp"
#include "4-lifecycle/memory/allocation.hpp"
#include "4-lifecycle/memory/heap_statistics.hpp"
#include "4-lifecycle/memory/impl/heap_classes.hpp"
#include "4-lifecycle/memory/impl/heap_span.hpp"
#include "4-lifecycle/memory/impl/heap_state.hpp"
#include "4-lifecycle/memory/impl/heap_traps.hpp"
#include "4-lifecycle/memory/is_heap_geometry.hpp"
#include "4-lifecycle/memory/is_stateless_allocator.hpp"


// -- CA NAMESPACE -----------------------------------------------------------

namespace ca {


	// -- BASIC HEAP ---------------------------------------------------------

	namespace impl {

		struct heap_access;

	} // namespace impl


	/* @DOC A stateless allocator of size classes over spans of Pages, one heap per context of
	   Binding: no lock and no atomic on a context's own blocks, one atomic exchange for a block
	   freed away from its heap. Usable size is the class size. Debug builds check that a block
	   goes back with its size class and alignment, and poison it. Under ca_memory_poisoning,
	   ASan sees what is not handed out (impl/heap_span.hpp) and a block freed twice traps. */
	template <ca::is_stateless_allocator Pages, ca::is_heap_geometry G, template <typename> typename Binding>
	class basic_heap final {


		private:

			// -- friends ----------------------------------------------------

			friend struct ca::impl::heap_access;


			// -- private types ----------------------------------------------

			using self = ca::basic_heap<Pages, G, Binding>;


		public:

			// -- public types -----------------------------------------------

			using pages_type      = Pages;
			using geometry_type   = G;
			using span_type       = ca::impl::heap_span<G>;
			using state_type      = ca::impl::heap_state<G>;
			using binding_type    = Binding<self>;
			using statistics_type = ca::heap_statistics<ca::impl::heap_class_count(G::LARGEST_CLASS)>;


			// -- public constants -------------------------------------------

			static constexpr bool STATELESS = true;

			static constexpr ca::usz ALIGNMENT = 16U;

			static constexpr ca::u32 CLASSES = ca::impl::heap_class_count(G::LARGEST_CLASS);


		private:

			// -- private members --------------------------------------------

			static constinit inline span_type _empty_span{};

			static constinit inline state_type _empty_state{ca::impl::heap_empty_queues<G>(&self::_empty_span)};


		public:

			// -- public lifecycle -------------------------------------------

			constexpr basic_heap(void) noexcept = default;


			// -- accessors --------------------------------------------------

			/* @DOC The heap of contexts that have not allocated yet: owns no span. */
			[[nodiscard]] static constexpr auto empty_state(void) noexcept -> state_type* {
				return &self::_empty_state;
			}


			// -- methods ----------------------------------------------------

			[[nodiscard, gnu::always_inline]] auto allocate(const ca::usz size, const ca::usz alignment) const noexcept -> ca::result<ca::allocation> {

				if (size - 1U < G::LARGEST_CLASS && alignment <= self::ALIGNMENT) [[likely]] {
					const auto pin = binding_type::pin();
					state_type* const heap = binding_type::current();
					const ca::u32 index = ca::impl::heap_class_of(size);
					span_type* const span = heap->_queues._spans[index];
					void* const block = span->_free;
					if (block != nullptr) [[likely]] {
						ca::impl::unpoison_bytes(block, span->_size);
						span->_free = *static_cast<void**>(block);
						++span->_used;
						if constexpr (G::CHECK_DOUBLE_FREE)
							ca::impl::heap_mark<G>(span, block);
						heap->_counters.add_blocks(index, 1U);
						binding_type::unpin(pin);
						return ca::allocation{block, span->_size};
					}
					binding_type::unpin(pin);
				}
				return self::_allocate(size, alignment);
			}

			[[gnu::always_inline]] auto deallocate(const ca::allocation block, const ca::usz alignment) const noexcept -> void {

				if (block.size() - 1U < G::LARGEST_CLASS && alignment <= self::ALIGNMENT) [[likely]] {
					span_type* const span = ca::impl::heap_span_of<G>(block.data());
					const auto pin = binding_type::pin();
					state_type* const owner = binding_type::current();
					if (span->_heap == owner) [[likely]] {
						if constexpr (ca_memory_poisoning)
							self::_check_live(block);
						if constexpr (ca_debug != 0)
							self::_check(span, block, alignment);
						if constexpr (G::CHECK_DOUBLE_FREE)
							ca::impl::heap_unmark<G>(span, block.data());
						void* const head = span->_free;
						*static_cast<void**>(block.data()) = head;
						ca::impl::poison_bytes(block.data(), span->_size);
						span->_free = block.data();
						--span->_used;
						owner->_counters.remove_blocks(span->_class, 1U);
						if (head == nullptr || (span->_used == 0U && owner->_queues._spans[span->_class] != span)) [[unlikely]]
							self::_settle(span);
						binding_type::unpin(pin);
						return;
					}
					binding_type::unpin(pin);
				}
				self::_deallocate(block, alignment);
			}

			/* @DOC allocate, zeroed: large blocks come zeroed from Pages when it can, small ones are
			   filled. */
			[[nodiscard]] auto allocate_zeroed(ca::usz size, ca::usz alignment) const noexcept -> ca::result<ca::allocation>;

			/* @DOC In place when size keeps the block's class, or both sizes are large and Pages
			   expands. */
			[[nodiscard]] auto expand(ca::allocation block, ca::usz size, ca::usz alignment) const noexcept -> ca::optional<ca::allocation>;

			/* @DOC In place when size keeps the class; large to large through Pages; otherwise a new
			   block and a copy. */
			[[nodiscard]] auto reallocate(ca::allocation block, ca::usz size, ca::usz alignment) const noexcept -> ca::result<ca::allocation>;

			/* @DOC Takes heap, its context gone for good, after the remote frees it has: its empty
			   spans go back, the rest waits in the pool for the next context. */
			static auto retire(state_type* heap) noexcept -> void;

			/* @DOC The counters of every heap, summed. */
			[[nodiscard]] static auto statistics(void) noexcept -> statistics_type
			requires (G::STATISTICS);


		private:

			// -- private methods --------------------------------------------

			[[nodiscard, gnu::noinline]] static auto _allocate(ca::usz size, ca::usz alignment) noexcept -> ca::result<ca::allocation>;

			[[gnu::noinline]] static auto _deallocate(ca::allocation block, ca::usz alignment) noexcept -> void;

			[[gnu::noinline]] static auto _settle(span_type* span) noexcept -> void;

			static auto _check(const span_type* span, ca::allocation block, ca::usz alignment) noexcept -> void;

			static auto _check_live(ca::allocation block) noexcept -> void;

	}; // class basic_heap

} // namespace ca

#endif // ca_lifecycle_memory_basic_heap_hpp
