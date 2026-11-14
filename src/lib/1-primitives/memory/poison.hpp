#ifndef ca_primitives_memory_poison_hpp
#define ca_primitives_memory_poison_hpp

#include "1-primitives/config/sanitizer.hpp"
#include "1-primitives/integers/usz.hpp"


// -- CA NAMESPACE -----------------------------------------------------------

namespace ca {


	// -- POISON -------------------------------------------------------------

	namespace impl {

#if ca_memory_poisoning
		extern "C" {
			auto __asan_poison_memory_region(const volatile void* address, ca::usz size) noexcept -> void;   // ns-leak: ignore
			auto __asan_unpoison_memory_region(const volatile void* address, ca::usz size) noexcept -> void; // ns-leak: ignore
			auto __asan_address_is_poisoned(const volatile void* address) noexcept -> int;                   // ns-leak: ignore
		}
#endif

		/* @DOC Hooks of an allocator for ASan, nothing unless ca_memory_poisoning: count bytes at
		   address marked unusable, an access then reported; ASan poisons only the 8-byte granules
		   the range covers whole. */
		[[gnu::always_inline]] inline auto poison_bytes([[maybe_unused]] const void* const address, [[maybe_unused]] const ca::usz count) noexcept -> void {
#if ca_memory_poisoning
			ca::impl::__asan_poison_memory_region(address, count);
#endif
		}

		/* @DOC count bytes at address usable again. */
		[[gnu::always_inline]] inline auto unpoison_bytes([[maybe_unused]] const void* const address, [[maybe_unused]] const ca::usz count) noexcept -> void {
#if ca_memory_poisoning
			ca::impl::__asan_unpoison_memory_region(address, count);
#endif
		}

		/* @DOC Whether the byte at address is poisoned; false unless ca_memory_poisoning. */
		[[nodiscard, gnu::always_inline]] inline auto is_poisoned([[maybe_unused]] const void* const address) noexcept -> bool {

#if ca_memory_poisoning
			return ca::impl::__asan_address_is_poisoned(address) != 0;
#else
			return false;
#endif
		}

	} // namespace impl

} // namespace ca

#endif // ca_primitives_memory_poison_hpp
