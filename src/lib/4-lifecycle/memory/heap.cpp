#include "1-primitives/integers/usz.hpp"
#include "3-system/sync/once.hpp"
#include "3-system/thread/exit_stage.hpp"
#include "3-system/thread/thread_key.hpp"
#include "3-system/thread/thread_key_create.hpp"
#include "3-system/thread/thread_key_set.hpp"
#include "4-lifecycle/memory/heap.hpp"
#include "4-lifecycle/memory/impl/heap_engine.hpp"


static constinit ca::once key_once{}; // file-state: ignore
static constinit ca::usz key_id = 0U; // file-state: ignore


// -- HEAP THREAD BINDING ----------------------------------------------------

template <>
auto ca::heap_thread_binding<ca::heap>::_exit(void* const heap) noexcept -> void {

	self::_current = ca::heap::empty_state();
	ca::heap::retire(static_cast<state*>(heap));
}

template <>
auto ca::heap_thread_binding<ca::heap>::adopt(state* const heap) noexcept -> void {

	const ca::result<void> created = key_once.call([](void) noexcept -> ca::result<void> {
		auto key = ca::sys::thread_key_create(&self::_exit, ca::sys::exit_stage::memory);
		if (!key)
			return key.propagate();
		key_id = key->id();
		return ca::result<void>{};
	});
	if (created)
		static_cast<void>(ca::sys::thread_key_set(ca::sys::thread_key{key_id}, heap));
	self::_current = heap;
}


// -- HEAP -------------------------------------------------------------------

template class ca::basic_heap<ca::page_allocator, ca::heap_geometry, ca::heap_thread_binding>;
