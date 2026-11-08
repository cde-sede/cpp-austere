#ifndef ca_collections_map_impl_control_group_hpp
#define ca_collections_map_impl_control_group_hpp

#include "1-primitives/config/architecture.hpp"
#include "1-primitives/integers/u8.hpp"
#include "5-collections/map/impl/swar_group.hpp"
#include "5-collections/map/impl/vector_group.hpp"


// -- CA NAMESPACE -----------------------------------------------------------

namespace ca {


	// -- IMPL ---------------------------------------------------------------

	namespace impl {


		// -- CONTROL GROUP --------------------------------------------------

#if ca_architecture(x86_64) && ca_target_feature(sse2)

		/* @DOC The control group of the hash tables on this CPU: 16 bytes in an SSE2 vector on
		   x86-64, 8 in a 64-bit word (SWAR) elsewhere and in an integer-only build. */
		using control_group = ca::impl::vector_group;

#else

		using control_group = ca::impl::swar_group;

#endif

		/* @DOC The control bytes of every table owning no storage: one group of EMPTY, so lookups
		   need no branch for it. Never written. */
		alignas(16) inline constinit ca::u8 empty_control[16] = {
			0x80U, 0x80U, 0x80U, 0x80U, 0x80U, 0x80U, 0x80U, 0x80U,
			0x80U, 0x80U, 0x80U, 0x80U, 0x80U, 0x80U, 0x80U, 0x80U,
		};

		static_assert(ca::impl::control_group::WIDTH <= sizeof(ca::impl::empty_control));

	} // namespace impl

} // namespace ca

#endif // ca_collections_map_impl_control_group_hpp
