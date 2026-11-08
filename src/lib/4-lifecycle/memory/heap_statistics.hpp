#ifndef ca_lifecycle_memory_heap_statistics_hpp
#define ca_lifecycle_memory_heap_statistics_hpp

#include "1-primitives/integers/u32.hpp"
#include "1-primitives/integers/usz.hpp"
#include "4-lifecycle/memory/heap_class_statistics.hpp"


// -- CA NAMESPACE -----------------------------------------------------------

namespace ca {


	// -- HEAP STATISTICS ----------------------------------------------------

	/* @DOC What a ca::basic_heap with N classes holds, summed over its heaps: per class, then the
	   spare spans, the heaps, and the large blocks with their bytes (the page source's usable
	   sizes). Blocks other threads freed count until their heap collects them. */
	template <ca::u32 N>
	struct heap_statistics final {
			using self = ca::heap_statistics<N>;

			ca::heap_class_statistics _classes[N];
			ca::usz                   _spare_spans;
			ca::usz                   _heaps;
			ca::usz                   _large_blocks;
			ca::usz                   _large_bytes;

			/* @DOC Small blocks in use, every class. */
			[[nodiscard]] constexpr auto small_blocks(void) const noexcept -> ca::usz {

				ca::usz total = 0U;
				for (const ca::heap_class_statistics& klass : _classes)
					total += klass._blocks;
				return total;
			}

			/* @DOC Bytes of the small blocks in use, at their class sizes. */
			[[nodiscard]] constexpr auto small_bytes(void) const noexcept -> ca::usz {

				ca::usz total = 0U;
				for (const ca::heap_class_statistics& klass : _classes)
					total += klass._blocks * klass._size;
				return total;
			}

			/* @DOC Spans held, spares included. */
			[[nodiscard]] constexpr auto spans(void) const noexcept -> ca::usz {

				ca::usz total = _spare_spans;
				for (const ca::heap_class_statistics& klass : _classes)
					total += klass._spans;
				return total;
			}
	}; // struct heap_statistics

} // namespace ca

#endif // ca_lifecycle_memory_heap_statistics_hpp
