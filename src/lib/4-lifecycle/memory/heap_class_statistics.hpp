#ifndef ca_lifecycle_memory_heap_class_statistics_hpp
#define ca_lifecycle_memory_heap_class_statistics_hpp

#include "1-primitives/integers/usz.hpp"


// -- CA NAMESPACE -----------------------------------------------------------

namespace ca {


	// -- HEAP CLASS STATISTICS ----------------------------------------------

	/* @DOC One size class of a heap: its block size, its blocks in use, the spans it holds. */
	struct heap_class_statistics final {
			using self = ca::heap_class_statistics;

			ca::usz _size;
			ca::usz _blocks;
			ca::usz _spans;
	}; // struct heap_class_statistics

} // namespace ca

#endif // ca_lifecycle_memory_heap_class_statistics_hpp
