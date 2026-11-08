#ifndef ca_lifecycle_memory_impl_heap_span_hpp
#define ca_lifecycle_memory_impl_heap_span_hpp

#include "1-primitives/atomic/atomic.hpp"
#include "1-primitives/integers/u32.hpp"
#include "1-primitives/integers/u64.hpp"
#include "1-primitives/integers/usz.hpp"
#include "4-lifecycle/memory/impl/heap_traps.hpp"


// -- CA NAMESPACE -----------------------------------------------------------

namespace ca {


	// -- HEAP SPAN ----------------------------------------------------------

	namespace impl {

		template <typename G>
		struct heap_state;


		/* @DOC Header of a heap span: G::SPAN_SIZE bytes aligned to their size, blocks of one class
		   from the start, this header in the last bytes, so a block finds it by masking its address.
		   The first line is the owner's alone. The second takes frees from other threads: a list
		   of blocks, or the full mark once the owner found nothing left, frees then going to the
		   owner's heap instead. A thread freeing a block touches the span no more after it. With
		   G::CHECK_DOUBLE_FREE, the marks of the blocks in use (a bit per 16 bytes) sit right
		   below the header. */
		template <typename G>
		struct alignas(64) heap_span final {
				using self = ca::impl::heap_span<G>;

				void*                         _free;
				ca::u32                       _used;
				ca::u32                       _class;
				ca::usz                       _size;
				char*                         _unused;
				char*                         _limit;
				ca::impl::heap_state<G>*      _heap;
				self*                         _next;
				self*                         _previous;
				alignas(64) ca::atomic<void*> _remote;
		}; // struct heap_span


		/* @DOC Words of block marks below a span's header: none without G::CHECK_DOUBLE_FREE. */
		template <typename G>
		inline constexpr ca::usz heap_mark_words = G::CHECK_DOUBLE_FREE ? G::SPAN_SIZE / 16U / 64U : 0U;

		/* @DOC Offset of the header in a span. */
		template <typename G>
		inline constexpr ca::usz heap_span_header = G::SPAN_SIZE - sizeof(ca::impl::heap_span<G>);

		/* @DOC Offset where blocks stop: the marks, else the header. */
		template <typename G>
		inline constexpr ca::usz heap_blocks_end = ca::impl::heap_span_header<G> - ca::impl::heap_mark_words<G> * sizeof(ca::u64);

		/* @DOC Header of the span holding block, a small heap block. */
		template <typename G>
		[[nodiscard, gnu::always_inline]] inline auto heap_span_of(void* const block) noexcept -> ca::impl::heap_span<G>* {

			const ca::usz base = reinterpret_cast<ca::usz>(block) & ~(G::SPAN_SIZE - 1U);
			return reinterpret_cast<ca::impl::heap_span<G>*>(base + ca::impl::heap_span_header<G>);
		}

		/* @DOC The block marks of span. */
		template <typename G>
		[[nodiscard, gnu::always_inline]] inline auto heap_marks_of(ca::impl::heap_span<G>* const span) noexcept -> ca::u64* {
			return reinterpret_cast<ca::u64*>(span) - ca::impl::heap_mark_words<G>;
		}

		/* @DOC Index of block's mark: its offset in its span, in units of 16 bytes. */
		template <typename G>
		[[nodiscard, gnu::always_inline]] inline auto heap_mark_of(const void* const block) noexcept -> ca::usz {
			return (reinterpret_cast<ca::usz>(block) & (G::SPAN_SIZE - 1U)) >> 4U;
		}

		/* @DOC Marks block, of span, in use. */
		template <typename G>
		[[gnu::always_inline]] inline auto heap_mark(ca::impl::heap_span<G>* const span, const void* const block) noexcept -> void {

			const ca::usz bit = ca::impl::heap_mark_of<G>(block);
			ca::impl::heap_marks_of<G>(span)[bit / 64U] |= ca::u64{1U} << (bit % 64U);
		}

		/* @DOC Clears block's mark; traps when block is not on a 16-byte boundary, or its mark was
		   clear. */
		template <typename G>
		[[gnu::always_inline]] inline auto heap_unmark(ca::impl::heap_span<G>* const span, const void* const block) noexcept -> void {

			if ((reinterpret_cast<ca::usz>(block) & 15U) != 0U) [[unlikely]]
				ca::impl::heap_pointer_freed_is_no_heap_block();
			const ca::usz bit = ca::impl::heap_mark_of<G>(block);
			ca::u64& word = ca::impl::heap_marks_of<G>(span)[bit / 64U];
			const ca::u64 mask = ca::u64{1U} << (bit % 64U);
			if ((word & mask) == 0U) [[unlikely]]
				ca::impl::heap_block_freed_twice_or_never_allocated();
			word &= ~mask;
		}

	} // namespace impl

} // namespace ca

#endif // ca_lifecycle_memory_impl_heap_span_hpp
