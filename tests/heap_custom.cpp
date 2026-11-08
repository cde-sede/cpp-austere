// expect: exit 0
// link: both
#include "1-primitives/debug/trap.hpp"
#include "1-primitives/integers/u32.hpp"
#include "1-primitives/integers/u8.hpp"
#include "1-primitives/integers/usz.hpp"
#include "1-primitives/memory/fill_bytes.hpp"
#include "4-lifecycle/memory/basic_heap.hpp"
#include "4-lifecycle/memory/impl/heap_engine.hpp"
#include "4-lifecycle/memory/page_allocator.hpp"

struct dirty_pages final {
		using self = dirty_pages;

		static constexpr bool STATELESS = true;
		static constexpr ca::usz ALIGNMENT = 4096U;

		static constinit inline ca::usz live = 0U;

		[[nodiscard]] auto allocate(const ca::usz size, const ca::usz alignment) const noexcept -> ca::result<ca::allocation> {

			auto block = ca::page_allocator{}.allocate(size, alignment);
			if (block) {
				ca::fill_bytes(block->data(), 0xABU, block->size());
				++self::live;
			}
			return block;
		}

		auto deallocate(const ca::allocation block, const ca::usz alignment) const noexcept -> void {
			--self::live;
			ca::page_allocator{}.deallocate(block, alignment);
		}
};

struct small_geometry final {
		static constexpr ca::usz SPAN_SIZE = 65536U;
		static constexpr ca::usz LARGEST_CLASS = 2048U;
		static constexpr ca::u32 SPARE_SPANS = 0U;
		static constexpr ca::usz STATE_CHUNK = 4096U;
		static constexpr bool STATISTICS = true;
		static constexpr bool CHECK_DOUBLE_FREE = true;
};

template <typename Heap>
struct pinned_binding final {
		using state = typename Heap::state_type;

		static constinit inline state* _current = Heap::empty_state();
		static constinit inline ca::u32 _depth = 0U;
		static constinit inline ca::usz _pins = 0U;

		struct token final {
				ca::u32 _depth;
		};

		[[nodiscard]] static auto current(void) noexcept -> state* {
			if (_depth == 0U)
				ca::trap();
			return _current;
		}

		[[nodiscard]] static auto pin(void) noexcept -> token {
			++_pins;
			return token{_depth++};
		}

		static auto unpin(const token pinned) noexcept -> void {
			if (--_depth != pinned._depth)
				ca::trap();
		}

		static auto adopt(state* const heap) noexcept -> void {
			if (_depth == 0U)
				ca::trap();
			_current = heap;
		}
};

using test_heap = ca::basic_heap<dirty_pages, small_geometry, pinned_binding>;
template class ca::basic_heap<dirty_pages, small_geometry, pinned_binding>;

static_assert(test_heap::CLASSES == 24U);
static_assert(ca::is_stateless_allocator<test_heap>);

static auto spans_match(void) noexcept -> bool {

	const test_heap::statistics_type now = test_heap::statistics();
	return dirty_pages::live == now.spans() + now._large_blocks + 1U;
}

auto main(void) -> int {

	const test_heap heap{};
	void* blocks[3000];
	for (ca::usz i = 0U; i < 3000U; ++i) {
		auto block = heap.allocate(1U + i % 2048U, 8U);
		if (!block || block->size() < 1U + i % 2048U)
			return 1;
		ca::fill_bytes(block->data(), static_cast<ca::u8>(i), block->size());
		blocks[i] = block->data();
	}
	test_heap::statistics_type full = test_heap::statistics();
	if (full.small_blocks() != 3000U || full._heaps != 1U || full._large_blocks != 0U)
		return 2;
	if (full._classes[0]._size != 16U || full._classes[23]._size != 2048U || full._classes[23]._blocks == 0U)
		return 3;
	if (!spans_match())
		return 4;
	ca::usz bytes = 0U;
	for (ca::usz i = 0U; i < 3000U; ++i)
		bytes += ca::impl::heap_class_size(ca::impl::heap_class_of(1U + i % 2048U));
	if (full.small_bytes() != bytes)
		return 5;

	for (ca::usz i = 0U; i < 3000U; ++i)
		heap.deallocate(ca::allocation{blocks[i], 1U + i % 2048U}, 8U);
	const test_heap::statistics_type empty = test_heap::statistics();
	if (empty.small_blocks() != 0U || empty._spare_spans != 0U)
		return 6;
	for (const ca::heap_class_statistics& klass : empty._classes)
		if (klass._spans > 1U)
			return 7;
	if (!spans_match())
		return 8;

	auto large = heap.allocate(10000U, 8U);
	if (!large || test_heap::statistics()._large_blocks != 1U || test_heap::statistics()._large_bytes != 12288U)
		return 9;
	auto grown = heap.reallocate(*large, 50000U, 8U);
	if (!grown || test_heap::statistics()._large_blocks != 1U || test_heap::statistics()._large_bytes != 53248U)
		return 10;
	heap.deallocate(grown->with_size(50000U), 8U);
	if (test_heap::statistics()._large_blocks != 0U || test_heap::statistics()._large_bytes != 0U)
		return 11;

	auto aligned = heap.allocate(100U, 1024U);
	if (!aligned || (reinterpret_cast<ca::usz>(aligned->data()) & 1023U) != 0U || aligned->size() != 1024U)
		return 12;
	heap.deallocate(aligned->with_size(100U), 1024U);

	auto zeroed = heap.allocate_zeroed(3000U, 8U);
	if (!zeroed || static_cast<ca::u8*>(zeroed->data())[2999] != 0U)
		return 13;
	heap.deallocate(*zeroed, 8U);

	if (pinned_binding<test_heap>::_depth != 0U || pinned_binding<test_heap>::_pins < 6000U)
		return 14;
	if (test_heap::statistics().small_blocks() != 0U || !spans_match())
		return 15;
	return 0;
}
