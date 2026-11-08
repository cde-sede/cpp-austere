// expect: trap
// link: both
#include "1-primitives/integers/u32.hpp"
#include "1-primitives/integers/usz.hpp"
#include "4-lifecycle/memory/basic_heap.hpp"
#include "4-lifecycle/memory/impl/heap_engine.hpp"
#include "4-lifecycle/memory/page_allocator.hpp"

struct checked_geometry final {
		static constexpr ca::usz SPAN_SIZE = 65536U;
		static constexpr ca::usz LARGEST_CLASS = 2048U;
		static constexpr ca::u32 SPARE_SPANS = 0U;
		static constexpr ca::usz STATE_CHUNK = 4096U;
		static constexpr bool STATISTICS = false;
		static constexpr bool CHECK_DOUBLE_FREE = true;
};

template <typename Heap>
struct global_binding final {
		using state = typename Heap::state_type;

		static constinit inline state* _current = Heap::empty_state();

		struct token final {
		};

		[[nodiscard]] static auto current(void) noexcept -> state* {
			return _current;
		}

		[[nodiscard]] static auto pin(void) noexcept -> token {
			return token{};
		}

		static auto unpin(const token) noexcept -> void {
		}

		static auto adopt(state* const heap) noexcept -> void {
			_current = heap;
		}
};

using test_heap = ca::basic_heap<ca::page_allocator, checked_geometry, global_binding>;
template class ca::basic_heap<ca::page_allocator, checked_geometry, global_binding>;

auto main(void) -> int {

	const test_heap heap{};
	auto block = heap.allocate(64U, 8U);
	if (!block)
		return 1;
	heap.deallocate(ca::allocation{static_cast<char*>(block->data()) + 8U, 56U}, 8U);
	return 0;
}
