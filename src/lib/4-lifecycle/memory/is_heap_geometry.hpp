#ifndef ca_lifecycle_memory_is_heap_geometry_hpp
#define ca_lifecycle_memory_is_heap_geometry_hpp

#include "1-primitives/bit/has_single_bit.hpp"
#include "1-primitives/integers/u32.hpp"
#include "1-primitives/integers/usz.hpp"
#include "1-primitives/traits/is_same.hpp"
#include "4-lifecycle/memory/impl/heap_classes.hpp"


// -- CA NAMESPACE -----------------------------------------------------------

namespace ca {


	// -- IS HEAP GEOMETRY ---------------------------------------------------

	namespace impl {

		template <typename G>
		concept has_heap_geometry_members = requires {
			requires ca::is_same<decltype(G::SPAN_SIZE), const ca::usz>;
			requires ca::is_same<decltype(G::LARGEST_CLASS), const ca::usz>;
			requires ca::is_same<decltype(G::SPARE_SPANS), const ca::u32>;
			requires ca::is_same<decltype(G::STATE_CHUNK), const ca::usz>;
			requires ca::is_same<decltype(G::STATISTICS), const bool>;
			requires ca::is_same<decltype(G::CHECK_DOUBLE_FREE), const bool>;
		};

		template <typename G>
		[[nodiscard]] consteval auto is_valid_heap_geometry(void) noexcept -> bool {
			return ca::has_single_bit(G::SPAN_SIZE) && G::SPAN_SIZE >= 16384U
			    && ca::has_single_bit(G::LARGEST_CLASS) && G::LARGEST_CLASS >= 16U
			    && ca::impl::heap_class_count(G::LARGEST_CLASS) <= ca::impl::heap_class_limit
			    && G::LARGEST_CLASS * 8U <= G::SPAN_SIZE
			    && G::STATE_CHUNK != 0U;
		}

	} // namespace impl


	/* @DOC The shape of a ca::basic_heap, as static constexpr members:
	   - SPAN_SIZE (usz): bytes of a span, a power of two of at least 16 KiB, asked of the page
	     source aligned to itself;
	   - LARGEST_CLASS (usz): the largest size a span serves, a power of two up to 128 KiB and an
	     eighth of SPAN_SIZE at most; larger blocks come from the page source;
	   - SPARE_SPANS (u32): emptied spans a heap keeps for reuse instead of returning them;
	   - STATE_CHUNK (usz): bytes asked of the page source at a time for heap states;
	   - STATISTICS (bool): per heap and class counters, read by statistics();
	   - CHECK_DOUBLE_FREE (bool): a bit per 16 bytes of each span, so that a block freed twice, or
	     never handed out, or an address off a 16-byte boundary, traps. */
	template <typename G>
	concept is_heap_geometry = ca::impl::has_heap_geometry_members<G> && ca::impl::is_valid_heap_geometry<G>();

} // namespace ca

#endif // ca_lifecycle_memory_is_heap_geometry_hpp
