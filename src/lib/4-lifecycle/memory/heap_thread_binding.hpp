#ifndef ca_lifecycle_memory_heap_thread_binding_hpp
#define ca_lifecycle_memory_heap_thread_binding_hpp

#include "1-primitives/integers/usz.hpp"


// -- CA NAMESPACE -----------------------------------------------------------

namespace ca {


	// -- HEAP THREAD BINDING ------------------------------------------------

	/* @DOC The binding of ca::heap (see ca::basic_heap): one heap per thread, in a constinit
	   thread_local, retired by a thread key when the thread exits (exit stage memory), nothing
	   to pin. Its adopt is defined in heap.cpp, for ca::heap only. */
	template <typename Heap>
	class heap_thread_binding final {


		private:

			// -- private types ----------------------------------------------

			using self  = ca::heap_thread_binding<Heap>;
			using state = typename Heap::state_type;


		public:

			// -- public types -----------------------------------------------

			struct token final {
					using self = typename ca::heap_thread_binding<Heap>::token;
			}; // struct token


		private:

			// -- private members --------------------------------------------

			static constinit inline thread_local state* _current = Heap::empty_state();


		public:

			// -- methods ----------------------------------------------------

			[[nodiscard, gnu::always_inline]] static auto current(void) noexcept -> state* {
				return self::_current;
			}

			[[nodiscard, gnu::always_inline]] static auto pin(void) noexcept -> token {
				return token{};
			}

			[[gnu::always_inline]] static auto unpin(const token) noexcept -> void {
			}

			static auto adopt(state* heap) noexcept -> void;


		private:

			// -- private methods --------------------------------------------

			static auto _exit(void* heap) noexcept -> void;

	}; // class heap_thread_binding

} // namespace ca

#endif // ca_lifecycle_memory_heap_thread_binding_hpp
