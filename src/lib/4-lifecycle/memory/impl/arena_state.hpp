#ifndef ca_lifecycle_memory_impl_arena_state_hpp
#define ca_lifecycle_memory_impl_arena_state_hpp

#include "1-primitives/integers/usz.hpp"


// -- CA NAMESPACE -----------------------------------------------------------

namespace ca {


	// -- ARENA STATE --------------------------------------------------------

	namespace impl {

		/* @DOC Header of every arena chunk, at its start: the chunk before it and its size. */
		struct arena_chunk final {
				using self = ca::impl::arena_chunk;

				self*   _previous;
				ca::usz _size;
		}; // struct arena_chunk


		/* @DOC One bump region of an arena: blocks go from _top up to _end in its newest chunk,
		   _spare is a released chunk of _chunk_size bytes kept for the next growth, _other the
		   lane scopes alternate with. */
		struct arena_lane final {
				using self = ca::impl::arena_lane;

				char*                  _top;
				char*                  _end;
				ca::impl::arena_chunk* _chunk;
				ca::impl::arena_chunk* _spare;
				self*                  _other;
				ca::usz                _chunk_size;
		}; // struct arena_lane


		/* @DOC An arena, kept in its first chunk after the chunk header so that it never moves:
		   lane 0 starts in that chunk and serves arena::allocator(), lane 1 maps nothing until a
		   scope allocates in it. */
		struct arena_state final {
				using self = ca::impl::arena_state;

				ca::impl::arena_lane   _lanes[2];
				ca::impl::arena_chunk* _first;
		}; // struct arena_state

	} // namespace impl

} // namespace ca

#endif // ca_lifecycle_memory_impl_arena_state_hpp
