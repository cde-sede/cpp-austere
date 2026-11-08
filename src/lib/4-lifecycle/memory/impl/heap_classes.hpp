#ifndef ca_lifecycle_memory_impl_heap_classes_hpp
#define ca_lifecycle_memory_impl_heap_classes_hpp

#include "1-primitives/bit/bit_width.hpp"
#include "1-primitives/integers/u32.hpp"
#include "1-primitives/integers/usz.hpp"


// -- CA NAMESPACE -----------------------------------------------------------

namespace ca {


	// -- HEAP CLASSES -------------------------------------------------------

	namespace impl {

		/* @DOC Size classes of the heap: 16 to 128 by 16, then four per doubling, up to a geometry's
		   LARGEST_CLASS. Every class is a multiple of 16, the powers of two included. */
		inline constexpr ca::u32 heap_class_limit = 48U;

		/* @DOC Class of a size of at least 1. */
		[[nodiscard, gnu::always_inline]] constexpr auto heap_class_of(const ca::usz size) noexcept -> ca::u32 {

			const ca::usz last = size - 1U;
			if (last < 128U)
				return static_cast<ca::u32>(last >> 4U);
			const ca::u32 width = static_cast<ca::u32>(ca::bit_width(last));
			return 8U + (width - 8U) * 4U + static_cast<ca::u32>((last >> (width - 3U)) & 3U);
		}

		/* @DOC Block size of a class. */
		[[nodiscard]] constexpr auto heap_class_size(const ca::u32 index) noexcept -> ca::usz {

			if (index < 8U)
				return (index + 1U) * 16U;
			const ca::u32 width = 8U + (index - 8U) / 4U;
			const ca::usz step = ca::usz{1U} << (width - 3U);
			return (ca::usz{1U} << (width - 1U)) + ((index - 8U) % 4U + 1U) * step;
		}

		/* @DOC Number of classes up to largest, a class size. */
		[[nodiscard]] constexpr auto heap_class_count(const ca::usz largest) noexcept -> ca::u32 {
			return ca::impl::heap_class_of(largest) + 1U;
		}

		static_assert(ca::impl::heap_class_count(131072U) == ca::impl::heap_class_limit);
		static_assert(ca::impl::heap_class_count(2048U) == 24U);
		static_assert([] {
			for (ca::u32 index = 0U; index < ca::impl::heap_class_limit; ++index) {
				const ca::usz size = ca::impl::heap_class_size(index);
				const ca::usz below = index == 0U ? 0U : ca::impl::heap_class_size(index - 1U);
				if (size % 16U != 0U || ca::impl::heap_class_of(size) != index || ca::impl::heap_class_of(below + 1U) != index)
					return false;
			}
			return true;
		}());

	} // namespace impl

} // namespace ca

#endif // ca_lifecycle_memory_impl_heap_classes_hpp
