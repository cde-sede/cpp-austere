#ifndef ca_lifecycle_memory_heap_geometry_hpp
#define ca_lifecycle_memory_heap_geometry_hpp

#include "1-primitives/integers/u32.hpp"
#include "1-primitives/integers/usz.hpp"


// -- CA NAMESPACE -----------------------------------------------------------

namespace ca {


	// -- HEAP GEOMETRY ------------------------------------------------------

	/* @DOC The geometry of ca::heap (see ca::is_heap_geometry): 1 MiB spans, classes up to
	   128 KiB, two spare spans per heap, no counters, no double-free check. */
	struct heap_geometry final {
			using self = ca::heap_geometry;

			static constexpr ca::usz SPAN_SIZE = 1048576U;

			static constexpr ca::usz LARGEST_CLASS = 131072U;

			static constexpr ca::u32 SPARE_SPANS = 2U;

			static constexpr ca::usz STATE_CHUNK = 65536U;

			static constexpr bool STATISTICS = false;

			static constexpr bool CHECK_DOUBLE_FREE = false;
	}; // struct heap_geometry

} // namespace ca

#endif // ca_lifecycle_memory_heap_geometry_hpp
