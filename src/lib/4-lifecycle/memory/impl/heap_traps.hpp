#ifndef ca_lifecycle_memory_impl_heap_traps_hpp
#define ca_lifecycle_memory_impl_heap_traps_hpp

#include "1-primitives/debug/trap.hpp"


// -- CA NAMESPACE -----------------------------------------------------------

namespace ca {


	// -- HEAP TRAPS ---------------------------------------------------------

	namespace impl {

		[[noreturn, gnu::noinline, gnu::cold]] inline auto heap_block_freed_with_another_size_class(void) noexcept -> void {
			ca::trap();
		}

		[[noreturn, gnu::noinline, gnu::cold]] inline auto heap_pointer_freed_is_no_heap_block(void) noexcept -> void {
			ca::trap();
		}

		[[noreturn, gnu::noinline, gnu::cold]] inline auto heap_block_freed_twice_or_never_allocated(void) noexcept -> void {
			ca::trap();
		}

		[[noreturn, gnu::noinline, gnu::cold]] inline auto heap_span_from_pages_misaligned(void) noexcept -> void {
			ca::trap();
		}

	} // namespace impl

} // namespace ca

#endif // ca_lifecycle_memory_impl_heap_traps_hpp
