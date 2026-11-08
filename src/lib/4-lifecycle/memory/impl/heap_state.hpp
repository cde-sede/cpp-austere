#ifndef ca_lifecycle_memory_impl_heap_state_hpp
#define ca_lifecycle_memory_impl_heap_state_hpp

#include "1-primitives/atomic/atomic.hpp"
#include "1-primitives/atomic/memory_order.hpp"
#include "1-primitives/integers/u32.hpp"
#include "1-primitives/integers/usz.hpp"
#include "4-lifecycle/memory/impl/heap_classes.hpp"
#include "4-lifecycle/memory/impl/heap_span.hpp"


// -- CA NAMESPACE -----------------------------------------------------------

namespace ca {


	// -- HEAP STATE ---------------------------------------------------------

	namespace impl {

		/* @DOC Per class, the first span with room left (or the empty span), linked to the others. */
		template <typename G>
		struct heap_queues final {
				using self = ca::impl::heap_queues<G>;

				static constexpr ca::u32 CLASSES = ca::impl::heap_class_count(G::LARGEST_CLASS);

				ca::impl::heap_span<G>* _spans[CLASSES];
		}; // struct heap_queues


		/* @DOC A heap's counters with G::STATISTICS: per class, blocks in use and spans held; spare
		   spans. Only the owner writes them, so a relaxed load and store do; anyone may read them. */
		template <typename G, bool = G::STATISTICS>
		struct heap_counters final {
				using self = ca::impl::heap_counters<G, G::STATISTICS>;

				[[gnu::always_inline]] auto add_blocks(const ca::u32, const ca::usz) noexcept -> void {
				}

				[[gnu::always_inline]] auto remove_blocks(const ca::u32, const ca::usz) noexcept -> void {
				}

				[[gnu::always_inline]] auto add_span(const ca::u32) noexcept -> void {
				}

				[[gnu::always_inline]] auto remove_span(const ca::u32) noexcept -> void {
				}

				[[gnu::always_inline]] auto add_spare(const ca::usz) noexcept -> void {
				}
		}; // struct heap_counters

		template <typename G>
		struct heap_counters<G, true> final {
				using self = ca::impl::heap_counters<G, true>;

				static constexpr ca::u32 CLASSES = ca::impl::heap_class_count(G::LARGEST_CLASS);

				ca::atomic<ca::usz> _blocks[CLASSES];
				ca::atomic<ca::usz> _spans[CLASSES];
				ca::atomic<ca::usz> _spare;

				[[gnu::always_inline]] static auto bump(ca::atomic<ca::usz>& counter, const ca::usz delta) noexcept -> void {
					counter.store<ca::memory_order::relaxed>(counter.load<ca::memory_order::relaxed>() + delta);
				}

				[[gnu::always_inline]] auto add_blocks(const ca::u32 index, const ca::usz count) noexcept -> void {
					self::bump(_blocks[index], count);
				}

				[[gnu::always_inline]] auto remove_blocks(const ca::u32 index, const ca::usz count) noexcept -> void {
					self::bump(_blocks[index], 0U - count);
				}

				[[gnu::always_inline]] auto add_span(const ca::u32 index) noexcept -> void {
					self::bump(_spans[index], 1U);
				}

				[[gnu::always_inline]] auto remove_span(const ca::u32 index) noexcept -> void {
					self::bump(_spans[index], 0U - ca::usz{1U});
				}

				[[gnu::always_inline]] auto add_spare(const ca::usz delta) noexcept -> void {
					self::bump(_spare, delta);
				}
		}; // struct heap_counters


		/* @DOC A heap: one per thread or per CPU, as its binding says. Spans whose blocks are all out
		   leave their queue until one comes back. Heaps are never freed: a retired heap goes to a
		   pool, the next new thread adopts it with its spans; every heap is on the _next_all list.
		   _pending gathers blocks other threads freed into full spans. */
		template <typename G>
		struct heap_state final {
				using self = ca::impl::heap_state<G>;

				ca::impl::heap_queues<G>                    _queues;
				ca::impl::heap_span<G>*                     _spare;
				ca::u32                                     _spare_count;
				self*                                       _next_idle;
				self*                                       _next_all;
				alignas(64) ca::atomic<void*>               _pending;
				[[no_unique_address]] ca::impl::heap_counters<G> _counters;

				constexpr explicit heap_state(const ca::impl::heap_queues<G> queues) noexcept
				: _queues{queues}, _spare{nullptr}, _spare_count{0U}, _next_idle{nullptr}, _next_all{nullptr}, _pending{nullptr}, _counters{} {
				}
		}; // struct heap_state


		/* @DOC Queues with every class on empty, the span with nothing to give. */
		template <typename G>
		[[nodiscard]] consteval auto heap_empty_queues(ca::impl::heap_span<G>* const empty) noexcept -> ca::impl::heap_queues<G> {

			ca::impl::heap_queues<G> queues{};
			for (ca::impl::heap_span<G>*& span : queues._spans)
				span = empty;
			return queues;
		}

	} // namespace impl

} // namespace ca

#endif // ca_lifecycle_memory_impl_heap_state_hpp
