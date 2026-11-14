/* @DOC The out-of-line part of ca::basic_heap: included only by the translation unit that
   instantiates a heap (`template class ca::basic_heap<Pages, G, Binding>;`), so that every other
   one calls the slow paths without seeing them. */

#ifndef ca_lifecycle_memory_impl_heap_engine_hpp
#define ca_lifecycle_memory_impl_heap_engine_hpp

#include "1-primitives/atomic/atomic.hpp"
#include "1-primitives/atomic/memory_order.hpp"
#include "1-primitives/config/debug.hpp"
#include "1-primitives/integers/u32.hpp"
#include "1-primitives/integers/u64.hpp"
#include "1-primitives/integers/u8.hpp"
#include "1-primitives/integers/usz.hpp"
#include "1-primitives/memory/construct_at.hpp"
#include "1-primitives/memory/copy_bytes.hpp"
#include "1-primitives/memory/fill_bytes.hpp"
#include "1-primitives/memory/poison.hpp"
#include "1-primitives/numeric/max.hpp"
#include "1-primitives/numeric/min.hpp"
#include "1-primitives/traits/is_same.hpp"
#include "2-vocabulary/nullopt.hpp"
#include "4-lifecycle/memory/allocate_zeroed.hpp"
#include "4-lifecycle/memory/basic_heap.hpp"
#include "4-lifecycle/memory/expand.hpp"
#include "4-lifecycle/memory/reallocate.hpp"


// -- CA NAMESPACE -----------------------------------------------------------

namespace ca {


	// -- HEAP ENGINE --------------------------------------------------------

	namespace impl {

		template <typename Pages>
		concept has_usable_size = requires(const Pages& pages, const ca::usz size, const ca::usz alignment) {
			{ pages.usable_size(size, alignment) } -> ca::is_same<ca::usz>;
		};


		/* @DOC What of a ca::basic_heap its engine reaches. */
		struct heap_access final {
				using self = ca::impl::heap_access;

				template <typename Heap>
				[[nodiscard, gnu::always_inline]] static constexpr auto empty_span(void) noexcept -> typename Heap::span_type* {
					return &Heap::_empty_span;
				}

				template <typename Heap>
				[[gnu::always_inline]] static auto settle(typename Heap::span_type* const span) noexcept -> void {
					Heap::_settle(span);
				}
		}; // struct heap_access


		/* Internal to the translation unit instantiating a heap, and defined outside the struct, so
		   the helpers inline into the slow paths as file-static functions would. */
		namespace {

			/* @DOC The state and the helpers of one heap instantiation: the pool of heap states under
			   a spin lock, the list of every heap, the large-block counters. */
			template <typename Heap>
			struct heap_engine final {
					using self     = ca::impl::heap_engine<Heap>;
					using pages    = typename Heap::pages_type;
					using geometry = typename Heap::geometry_type;
					using binding  = typename Heap::binding_type;
					using span     = typename Heap::span_type;
					using state    = typename Heap::state_type;

					static constexpr ca::usz CARVE_BYTES = 4096U;
					static constexpr ca::u8  POISON_BYTE = 0xDFU;

					static constinit inline ca::atomic<ca::u32> _pool_lock{0U};
					static constinit inline state*               _idle      = nullptr;
					static constinit inline state*               _all       = nullptr;
					static constinit inline char*                _pool_next = nullptr;
					static constinit inline char*                _pool_end  = nullptr;
					static constinit inline ca::atomic<ca::usz>  _large_blocks{0U};
					static constinit inline ca::atomic<ca::usz>  _large_bytes{0U};

					[[nodiscard]] static constexpr auto empty_span(void) noexcept -> span* {
						return ca::impl::heap_access::empty_span<Heap>();
					}

					[[nodiscard]] static auto is_small(ca::usz size, ca::usz alignment) noexcept -> bool;
					[[nodiscard]] static auto class_for(ca::usz size, ca::usz alignment) noexcept -> ca::u32;
					[[nodiscard]] static auto full_mark(void) noexcept -> void*;

					[[nodiscard]] static auto usable_size(ca::usz size, ca::usz alignment) noexcept -> ca::usz;
					static auto count_large(ca::usz blocks, ca::usz bytes) noexcept -> void;
					static auto count_new(const ca::result<ca::allocation>& block, ca::usz alignment) noexcept -> void;
					static auto count_moved(ca::allocation from, ca::allocation to, ca::usz alignment) noexcept -> void;

					static auto lock(void) noexcept -> void;
					static auto unlock(void) noexcept -> void;

					[[nodiscard]] static auto base_of(span* target) noexcept -> char*;
					static auto link_front(state* heap, span* target) noexcept -> void;
					static auto unlink(state* heap, span* target) noexcept -> void;
					static auto release(state* heap, span* target) noexcept -> void;
					[[nodiscard]] static auto fresh_span(state* heap, ca::u32 index) noexcept -> ca::result<span*>;
					static auto collect(span* target, void* list) noexcept -> void;
					[[nodiscard]] static auto carve(span* target) noexcept -> bool;
					static auto local_free(span* target, void* block) noexcept -> void;
					static auto remote_free(span* target, void* block) noexcept -> void;
					static auto take_pending(state* heap) noexcept -> void;
					[[nodiscard]] static auto allocate_from(state* heap, ca::u32 index) noexcept -> ca::result<ca::allocation>;

					[[nodiscard]] static auto acquire(void) noexcept -> ca::result<state*>;
			}; // struct heap_engine

		} // namespace

	} // namespace impl


	// -- SIZES --------------------------------------------------------------

	/* @DOC Whether a size class serves size at alignment. */
	template <typename Heap>
	[[nodiscard]] auto ca::impl::heap_engine<Heap>::is_small(const ca::usz size, const ca::usz alignment) noexcept -> bool {
		return size <= geometry::LARGEST_CLASS && alignment <= geometry::LARGEST_CLASS;
	}

	/* @DOC The smallest class holding size whose blocks are aligned to alignment. */
	template <typename Heap>
	[[nodiscard]] auto ca::impl::heap_engine<Heap>::class_for(const ca::usz size, const ca::usz alignment) noexcept -> ca::u32 {

		ca::u32 index = ca::impl::heap_class_of(size);
		if (alignment > Heap::ALIGNMENT) [[unlikely]]
			while ((ca::impl::heap_class_size(index) & (alignment - 1U)) != 0U)
				++index;
		return index;
	}

	/* @DOC The value of a span's remote list once its owner found it exhausted. */
	template <typename Heap>
	[[nodiscard]] auto ca::impl::heap_engine<Heap>::full_mark(void) noexcept -> void* {
		return reinterpret_cast<void*>(ca::usz{1U});
	}


	// -- LARGE BLOCKS -------------------------------------------------------

	/* @DOC Bytes a large block of size holds: pages' usable_size, else size in whole ALIGNMENTs. */
	template <typename Heap>
	[[nodiscard]] auto ca::impl::heap_engine<Heap>::usable_size(const ca::usz size, const ca::usz alignment) noexcept -> ca::usz {

		if constexpr (ca::impl::has_usable_size<pages>)
			return pages{}.usable_size(size, alignment);
		else
			return (size + pages::ALIGNMENT - 1U) & ~(pages::ALIGNMENT - 1U);
	}

	/* @DOC Adds to the large-block counters (wrapping, to subtract). */
	template <typename Heap>
	auto ca::impl::heap_engine<Heap>::count_large(const ca::usz blocks, const ca::usz bytes) noexcept -> void {

		self::_large_blocks.template fetch_add<ca::memory_order::relaxed>(blocks);
		self::_large_bytes.template fetch_add<ca::memory_order::relaxed>(bytes);
	}

	/* @DOC Counts a large block pages handed out, if it did. */
	template <typename Heap>
	auto ca::impl::heap_engine<Heap>::count_new(const ca::result<ca::allocation>& block, const ca::usz alignment) noexcept -> void {
		if (block && block->size() != 0U)
			self::count_large(1U, self::usable_size(block->size(), alignment));
	}

	/* @DOC Counts a large block resized from from to to. */
	template <typename Heap>
	auto ca::impl::heap_engine<Heap>::count_moved(const ca::allocation from, const ca::allocation to, const ca::usz alignment) noexcept -> void {
		self::count_large(0U, self::usable_size(to.size(), alignment) - self::usable_size(from.size(), alignment));
	}


	// -- POOL ---------------------------------------------------------------

	/* @DOC Takes the pool's spin lock. */
	template <typename Heap>
	auto ca::impl::heap_engine<Heap>::lock(void) noexcept -> void {
		while (self::_pool_lock.template exchange<ca::memory_order::acquire>(1U) != 0U)
			while (self::_pool_lock.template load<ca::memory_order::relaxed>() != 0U) {
			}
	}

	/* @DOC Releases the pool's spin lock. */
	template <typename Heap>
	auto ca::impl::heap_engine<Heap>::unlock(void) noexcept -> void {
		self::_pool_lock.template store<ca::memory_order::release>(0U);
	}


	// -- SPANS --------------------------------------------------------------

	/* @DOC The first byte of target's span. */
	template <typename Heap>
	[[nodiscard]] auto ca::impl::heap_engine<Heap>::base_of(span* const target) noexcept -> char* {
		return reinterpret_cast<char*>(target) - ca::impl::heap_span_header<geometry>;
	}

	/* @DOC Puts target first in its class's queue. */
	template <typename Heap>
	auto ca::impl::heap_engine<Heap>::link_front(state* const heap, span* const target) noexcept -> void {

		span*& first = heap->_queues._spans[target->_class];
		target->_previous = nullptr;
		    target->_next = first == self::empty_span() ? nullptr : first;
		if (target->_next != nullptr)
			target->_next->_previous = target;
		first = target;
	}

	/* @DOC Takes target out of its class's queue. */
	template <typename Heap>
	auto ca::impl::heap_engine<Heap>::unlink(state* const heap, span* const target) noexcept -> void {

		if (target->_previous != nullptr)
			target->_previous->_next = target->_next;
		else
			heap->_queues._spans[target->_class] = target->_next != nullptr ? target->_next : self::empty_span();
		if (target->_next != nullptr)
			target->_next->_previous = target->_previous;
		    target->_next = nullptr;
		target->_previous = nullptr;
	}

	/* @DOC An emptied span: kept as a spare while there is room, its blocks poisoned, else back to
	   pages, unpoisoned for whoever maps the range next. */
	template <typename Heap>
	auto ca::impl::heap_engine<Heap>::release(state* const heap, span* const target) noexcept -> void {

		heap->_counters.remove_span(target->_class);
		if (heap->_spare_count < geometry::SPARE_SPANS) {
			 target->_next = heap->_spare;
			  heap->_spare = target;
			++heap->_spare_count;
			heap->_counters.add_spare(1U);
			return;
		}
		ca::impl::unpoison_bytes(self::base_of(target), ca::impl::heap_blocks_end<geometry>);
		pages{}.deallocate(ca::allocation{self::base_of(target), geometry::SPAN_SIZE}, geometry::SPAN_SIZE);
	}

	/* @DOC A span for class index of heap: a spare, else new from pages, its marks cleared, its
	   blocks poisoned until carved. */
	template <typename Heap>
	[[nodiscard]] auto ca::impl::heap_engine<Heap>::fresh_span(state* const heap, const ca::u32 index) noexcept -> ca::result<span*> {

		char* base = nullptr;
		if (heap->_spare != nullptr) {
			        base = self::base_of(heap->_spare);
			heap->_spare = heap->_spare->_next;
			--heap->_spare_count;
			heap->_counters.add_spare(0U - ca::usz{1U});
		}
		else {
			auto mapped = pages{}.allocate(geometry::SPAN_SIZE, geometry::SPAN_SIZE);
			if (!mapped) [[unlikely]]
				return mapped.propagate();
			base = static_cast<char*>(mapped->data());
			if ((reinterpret_cast<ca::usz>(base) & (geometry::SPAN_SIZE - 1U)) != 0U) [[unlikely]]
				ca::impl::heap_span_from_pages_misaligned();
		}
		span* const target = ca::construct_at(reinterpret_cast<span*>(base + ca::impl::heap_span_header<geometry>));
		if constexpr (geometry::CHECK_DOUBLE_FREE)
			ca::fill_bytes(ca::impl::heap_marks_of<geometry>(target), 0U, ca::impl::heap_mark_words<geometry> * sizeof(ca::u64));
		const ca::usz size = ca::impl::heap_class_size(index);
		 target->_class = index;
		  target->_size = size;
		target->_unused = base;
		 target->_limit = base + (ca::impl::heap_blocks_end<geometry> / size) * size;
		  target->_heap = heap;
		ca::impl::poison_bytes(base, ca::impl::heap_blocks_end<geometry>);
		heap->_counters.add_span(index);
		return target;
	}

	/* @DOC Takes back list, blocks of target other heaps freed. */
	template <typename Heap>
	auto ca::impl::heap_engine<Heap>::collect(span* const target, void* const list) noexcept -> void {

		ca::u32 count = 1U;
		void* tail = list;
		if constexpr (geometry::CHECK_DOUBLE_FREE)
			ca::impl::heap_unmark<geometry>(target, tail);
		while (ca::impl::heap_link_of(tail) != nullptr) {
			tail = ca::impl::heap_link_of(tail);
			if constexpr (geometry::CHECK_DOUBLE_FREE)
				ca::impl::heap_unmark<geometry>(target, tail);
			++count;
		}
		ca::impl::heap_set_link(tail, target->_free);
		    target->_free = list;
		    target->_used -= count;
		target->_heap->_counters.remove_blocks(target->_class, count);
	}

	/* @DOC Links a page of target's untouched blocks into its free list; false when none is left. */
	template <typename Heap>
	[[nodiscard]] auto ca::impl::heap_engine<Heap>::carve(span* const target) noexcept -> bool {

		char* const start = target->_unused;
		if (start == target->_limit)
			return false;
		const ca::usz size = target->_size;
		ca::usz bytes = ca::max(size, CARVE_BYTES);
		bytes -= bytes % size;
		bytes = ca::min(bytes, static_cast<ca::usz>(target->_limit - start));
		char* const last = start + bytes - size;
		ca::impl::unpoison_bytes(start, bytes);
		for (char* block = start; block != last; block += size)
			*reinterpret_cast<void**>(block) = block + size;
		*reinterpret_cast<void**>(last) = nullptr;
		ca::impl::poison_bytes(start, bytes);
		  target->_free = start;
		target->_unused = start + bytes;
		return true;
	}

	/* @DOC Frees block into target, by its owner. */
	template <typename Heap>
	auto ca::impl::heap_engine<Heap>::local_free(span* const target, void* const block) noexcept -> void {

		if constexpr (geometry::CHECK_DOUBLE_FREE)
			ca::impl::heap_unmark<geometry>(target, block);
		void* const head = target->_free;
		ca::impl::heap_set_link(block, head);
		ca::impl::poison_bytes(block, target->_size);
		target->_free = block;
		--target->_used;
		target->_heap->_counters.remove_blocks(target->_class, 1U);
		if (head == nullptr || (target->_used == 0U && target->_heap->_queues._spans[target->_class] != target))
			ca::impl::heap_access::settle<Heap>(target);
	}

	/* @DOC Frees block of target from another heap: onto the span's list, or its heap's once full. */
	template <typename Heap>
	auto ca::impl::heap_engine<Heap>::remote_free(span* const target, void* const block) noexcept -> void {

		ca::impl::poison_bytes(block, target->_size);
		void* old = target->_remote.template load<ca::memory_order::relaxed>();
		for (;;) {
			if (old == self::full_mark()) {
				state* const heap = target->_heap;
				void* head = heap->_pending.template load<ca::memory_order::relaxed>();
				do
					ca::impl::heap_set_link(block, head);
				while (!heap->_pending.template compare_exchange_weak<ca::memory_order::release, ca::memory_order::relaxed>(head, block));
				return;
			}
			ca::impl::heap_set_link(block, old);
			if (target->_remote.template compare_exchange_weak<ca::memory_order::release, ca::memory_order::relaxed>(old, block))
				return;
		}
	}

	/* @DOC Frees the blocks other heaps sent heap. */
	template <typename Heap>
	auto ca::impl::heap_engine<Heap>::take_pending(state* const heap) noexcept -> void {

		void* block = heap->_pending.template exchange<ca::memory_order::acquire>(nullptr);
		while (block != nullptr) {
			void* const next = ca::impl::heap_link_of(block);
			self::local_free(ca::impl::heap_span_of<geometry>(block), block);
			block = next;
		}
	}

	/* @DOC A block of class index: remote frees, else carving, else the next span or a new one. */
	template <typename Heap>
	[[nodiscard]] auto ca::impl::heap_engine<Heap>::allocate_from(state* const heap, const ca::u32 index) noexcept -> ca::result<ca::allocation> {
		for (;;) {
			span* const target = heap->_queues._spans[index];
			if (target != self::empty_span()) {
				if (target->_free == nullptr) {
					void* const remote = target->_remote.template exchange<ca::memory_order::acquire>(nullptr);
					if (remote != nullptr)
						self::collect(target, remote);
					else if (!self::carve(target)) {
						void* expected = nullptr;
						if (target->_remote.template compare_exchange_strong<ca::memory_order::acquire>(expected, self::full_mark())) {
							self::unlink(heap, target);
							continue;
						}
						self::collect(target, target->_remote.template exchange<ca::memory_order::acquire>(nullptr));
					}
				}
				void* const block = target->_free;
				ca::impl::unpoison_bytes(block, target->_size);
				target->_free = *static_cast<void**>(block);
				++target->_used;
				if constexpr (geometry::CHECK_DOUBLE_FREE)
					ca::impl::heap_mark<geometry>(target, block);
				heap->_counters.add_blocks(index, 1U);
				return ca::allocation{block, target->_size};
			}
			if (heap->_pending.template load<ca::memory_order::relaxed>() != nullptr) {
				self::take_pending(heap);
				continue;
			}
			auto fresh = self::fresh_span(heap, index);
			if (!fresh) [[unlikely]]
				return fresh.propagate();
			self::link_front(heap, *fresh);
		}
	}


	// -- HEAPS --------------------------------------------------------------

	/* @DOC A heap for the calling context, idle or new, given to the binding. */
	template <typename Heap>
	[[nodiscard]] auto ca::impl::heap_engine<Heap>::acquire(void) noexcept -> ca::result<state*> {

		self::lock();
		state* heap = self::_idle;
		if (heap != nullptr)
			self::_idle = heap->_next_idle;
		else {
			if (static_cast<ca::usz>(self::_pool_end - self::_pool_next) < sizeof(state)) {
				auto chunk = pages{}.allocate(geometry::STATE_CHUNK, alignof(state));
				if (!chunk) [[unlikely]] {
					self::unlock();
					return chunk.propagate();
				}
				self::_pool_next = static_cast<char*>(chunk->data());
				 self::_pool_end = self::_pool_next + chunk->size();
			}
			heap = ca::construct_at(reinterpret_cast<state*>(self::_pool_next), ca::impl::heap_empty_queues<geometry>(self::empty_span()));
			self::_pool_next += sizeof(state);
			heap->_next_all = self::_all;
			     self::_all = heap;
		}
		self::unlock();
		binding::adopt(heap);
		return heap;
	}


	// -- BASIC HEAP ---------------------------------------------------------

	/* @DOC allocate past the fast path: size 0, large, the first block, an aligned class, a refill. */
	template <ca::is_stateless_allocator Pages, ca::is_heap_geometry G, template <typename> typename Binding>
	[[nodiscard, gnu::noinline]] auto ca::basic_heap<Pages, G, Binding>::_allocate(const ca::usz size, const ca::usz alignment) noexcept -> ca::result<ca::allocation> {

		using engine = ca::impl::heap_engine<self>;
		if (size == 0U) [[unlikely]]
			return ca::allocation::empty(alignment);
		if (!engine::is_small(size, alignment)) {
			if constexpr (!G::STATISTICS)
				return Pages{}.allocate(size, alignment);
			else {
				ca::result<ca::allocation> block = Pages{}.allocate(size, alignment);
				engine::count_new(block, alignment);
				return block;
			}
		}
		const auto pin = binding_type::pin();
		state_type* heap = binding_type::current();
		if (heap == &self::_empty_state) [[unlikely]] {
			auto acquired = engine::acquire();
			if (!acquired) [[unlikely]] {
				binding_type::unpin(pin);
				return acquired.propagate();
			}
			heap = *acquired;
		}
		ca::result<ca::allocation> block = engine::allocate_from(heap, engine::class_for(size, alignment));
		binding_type::unpin(pin);
		return block;
	}

	/* @DOC deallocate past the fast path: size 0, large, an aligned class, another heap's block. */
	template <ca::is_stateless_allocator Pages, ca::is_heap_geometry G, template <typename> typename Binding>
	[[gnu::noinline]] auto ca::basic_heap<Pages, G, Binding>::_deallocate(const ca::allocation block, const ca::usz alignment) noexcept -> void {

		using engine = ca::impl::heap_engine<self>;
		if (block.size() == 0U) [[unlikely]]
			return;
		if (!engine::is_small(block.size(), alignment)) {
			Pages{}.deallocate(block, alignment);
			if constexpr (G::STATISTICS)
				engine::count_large(0U - ca::usz{1U}, 0U - engine::usable_size(block.size(), alignment));
			return;
		}
		span_type* const span = ca::impl::heap_span_of<G>(block.data());
		if constexpr (ca_memory_poisoning)
			self::_check_live(block);
		if constexpr (ca_debug != 0)
			self::_check(span, block, alignment);
		const auto pin = binding_type::pin();
		if (span->_heap == binding_type::current())
			engine::local_free(span, block.data());
		else
			engine::remote_free(span, block.data());
		binding_type::unpin(pin);
	}

	/* @DOC After a free: a full span back in its queue, an emptied one released, the class's first
	   span too once emptied and passed. */
	template <ca::is_stateless_allocator Pages, ca::is_heap_geometry G, template <typename> typename Binding>
	[[gnu::noinline]] auto ca::basic_heap<Pages, G, Binding>::_settle(span_type* const span) noexcept -> void {

		using engine = ca::impl::heap_engine<self>;
		state_type* const heap = span->_heap;
		if (span->_remote.template load<ca::memory_order::relaxed>() == engine::full_mark()) {
			span->_remote.template store<ca::memory_order::relaxed>(nullptr);
			span_type* const first = heap->_queues._spans[span->_class];
			engine::link_front(heap, span);
			if (first != &self::_empty_span && first->_used == 0U) {
				engine::unlink(heap, first);
				engine::release(heap, first);
			}
			return;
		}
		if (span->_used == 0U && heap->_queues._spans[span->_class] != span) {
			engine::unlink(heap, span);
			engine::release(heap, span);
		}
	}

	/* @DOC Debug: block starts a block of span, has its class, and is poisoned. */
	template <ca::is_stateless_allocator Pages, ca::is_heap_geometry G, template <typename> typename Binding>
	auto ca::basic_heap<Pages, G, Binding>::_check(const span_type* const span, const ca::allocation block, const ca::usz alignment) noexcept -> void {

		using engine = ca::impl::heap_engine<self>;
		char* const data = static_cast<char*>(block.data());
		const char* const base = reinterpret_cast<const char*>(span) - ca::impl::heap_span_header<G>;
		if (data >= span->_limit || static_cast<ca::usz>(data - base) % span->_size != 0U) [[unlikely]]
			ca::impl::heap_pointer_freed_is_no_heap_block();
		if (span->_class != engine::class_for(block.size(), alignment)) [[unlikely]]
			ca::impl::heap_block_freed_with_another_size_class();
		ca::fill_bytes(data + sizeof(void*), engine::POISON_BYTE, span->_size - sizeof(void*));
	}

	/* @DOC Under ca_memory_poisoning: block, small, is in use, its first byte not poisoned; else
	   it was freed already, or never handed out. */
	template <ca::is_stateless_allocator Pages, ca::is_heap_geometry G, template <typename> typename Binding>
	auto ca::basic_heap<Pages, G, Binding>::_check_live(const ca::allocation block) noexcept -> void {
		if (ca::impl::is_poisoned(block.data())) [[unlikely]]
			ca::impl::heap_block_freed_twice_or_never_allocated();
	}

	/* @DOC Large blocks from pages' allocate_zeroed, small ones filled. */
	template <ca::is_stateless_allocator Pages, ca::is_heap_geometry G, template <typename> typename Binding>
	[[nodiscard]] auto ca::basic_heap<Pages, G, Binding>::allocate_zeroed(const ca::usz size, const ca::usz alignment) const noexcept -> ca::result<ca::allocation> {

		using engine = ca::impl::heap_engine<self>;
		if (!engine::is_small(size, alignment)) {
			if constexpr (!G::STATISTICS)
				return ca::allocate_zeroed(Pages{}, size, alignment);
			else {
				ca::result<ca::allocation> block = ca::allocate_zeroed(Pages{}, size, alignment);
				engine::count_new(block, alignment);
				return block;
			}
		}
		auto block = this->allocate(size, alignment);
		if (block) [[likely]]
			ca::fill_bytes(block->data(), 0U, block->size());
		return block;
	}

	/* @DOC Large to large through pages, small within the class. */
	template <ca::is_stateless_allocator Pages, ca::is_heap_geometry G, template <typename> typename Binding>
	[[nodiscard]] auto ca::basic_heap<Pages, G, Binding>::expand(const ca::allocation block, const ca::usz size, const ca::usz alignment) const noexcept -> ca::optional<ca::allocation> {

		using engine = ca::impl::heap_engine<self>;
		if (block.size() == 0U || size == 0U)
			return ca::nullopt;
		const bool was_small = engine::is_small(block.size(), alignment);
		const bool stays_small = engine::is_small(size, alignment);
		if (!was_small && !stays_small) {
			if constexpr (!G::STATISTICS)
				return ca::expand(Pages{}, block, size, alignment);
			else {
				ca::optional<ca::allocation> expanded = ca::expand(Pages{}, block, size, alignment);
				if (expanded)
					engine::count_moved(block, *expanded, alignment);
				return expanded;
			}
		}
		if (was_small && stays_small) {
			const span_type* const span = ca::impl::heap_span_of<G>(block.data());
			if (engine::class_for(size, alignment) == span->_class)
				return ca::allocation{block.data(), span->_size};
		}
		return ca::nullopt;
	}

	/* @DOC Large to large through pages, else expand, else move. */
	template <ca::is_stateless_allocator Pages, ca::is_heap_geometry G, template <typename> typename Binding>
	[[nodiscard]] auto ca::basic_heap<Pages, G, Binding>::reallocate(const ca::allocation block, const ca::usz size, const ca::usz alignment) const noexcept -> ca::result<ca::allocation> {

		using engine = ca::impl::heap_engine<self>;
		if (block.size() == 0U)
			return this->allocate(size, alignment);
		if (size == 0U) {
			this->deallocate(block, alignment);
			return ca::allocation::empty(alignment);
		}
		if (!engine::is_small(block.size(), alignment) && !engine::is_small(size, alignment)) {
			if constexpr (!G::STATISTICS)
				return ca::reallocate(Pages{}, block, size, alignment);
			else {
				ca::result<ca::allocation> moved = ca::reallocate(Pages{}, block, size, alignment);
				if (moved)
					engine::count_moved(block, *moved, alignment);
				return moved;
			}
		}
		auto in_place = this->expand(block, size, alignment);
		if (in_place)
			return *in_place;
		auto moved = this->allocate(size, alignment);
		if (!moved) [[unlikely]]
			return moved;
		ca::copy_bytes(moved->data(), block.data(), ca::min(block.size(), size));
		this->deallocate(block, alignment);
		return moved;
	}

	/* @DOC Pending frees taken, empty spans released, the heap pooled. */
	template <ca::is_stateless_allocator Pages, ca::is_heap_geometry G, template <typename> typename Binding>
	auto ca::basic_heap<Pages, G, Binding>::retire(state_type* const heap) noexcept -> void {

		using engine = ca::impl::heap_engine<self>;
		const auto pin = binding_type::pin();
		engine::take_pending(heap);
		for (span_type* span : heap->_queues._spans)
			while (span != &self::_empty_span && span != nullptr) {
				span_type* const next = span->_next;
				if (span->_used == 0U) {
					engine::unlink(heap, span);
					engine::release(heap, span);
				}
				span = next;
			}
		engine::lock();
		heap->_next_idle = engine::_idle;
		    engine::_idle = heap;
		engine::unlock();
		binding_type::unpin(pin);
	}

	/* @DOC Every heap's counters under the pool lock, then the large ones. */
	template <ca::is_stateless_allocator Pages, ca::is_heap_geometry G, template <typename> typename Binding>
	[[nodiscard]] auto ca::basic_heap<Pages, G, Binding>::statistics(void) noexcept -> statistics_type
	requires (G::STATISTICS) {

		using engine = ca::impl::heap_engine<self>;
		statistics_type totals{};
		for (ca::u32 index = 0U; index < self::CLASSES; ++index)
			totals._classes[index]._size = ca::impl::heap_class_size(index);
		const auto pin = binding_type::pin();
		engine::lock();
		for (const state_type* heap = engine::_all; heap != nullptr; heap = heap->_next_all) {
			for (ca::u32 index = 0U; index < self::CLASSES; ++index) {
				totals._classes[index]._blocks += heap->_counters._blocks[index].template load<ca::memory_order::relaxed>();
				 totals._classes[index]._spans += heap->_counters._spans[index].template load<ca::memory_order::relaxed>();
			}
			totals._spare_spans += heap->_counters._spare.template load<ca::memory_order::relaxed>();
			++totals._heaps;
		}
		engine::unlock();
		binding_type::unpin(pin);
		totals._large_blocks = engine::_large_blocks.template load<ca::memory_order::relaxed>();
		 totals._large_bytes = engine::_large_bytes.template load<ca::memory_order::relaxed>();
		return totals;
	}

} // namespace ca

#endif // ca_lifecycle_memory_impl_heap_engine_hpp
