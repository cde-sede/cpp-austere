// ca::arena and its scopes under random sequences of push, pop, allocations through the
// innermost scope and through its result(), deallocate and expand of the newest block. A model
// knows which scope each block dies with (level L, in lane L % 2: the arena's blocks are level
// 0, a scope at depth d allocates level d, its result() level d - 1): the blocks still alive
// keep their pattern, and every block a pop or a deallocate released is poisoned at once, when
// it starts a granule ASan can mark (8-byte aligned, 8 bytes or more), or gone with its chunk.
#include "1-primitives/memory/poison.hpp"
#include "4-lifecycle/memory/arena.hpp"
#include "fuzz/fuzz.hpp"

extern "C" auto mincore(void* address, ca::usz size, unsigned char* resident) noexcept -> int;

namespace {

	constexpr ca::usz BLOCKS = 256U;
	constexpr ca::usz DEPTH = 8U;
	constexpr ca::usz NONE = ~ca::usz{0U};

	struct block final {
			char* _data;
			ca::usz _size;
			ca::usz _level;
			char _tag;
			bool _alive;
	}; // struct block

	struct model final {
			block _blocks[BLOCKS];
			ca::usz _count;
			ca::usz _newest[2];
	}; // struct model

	auto mark(block& b, const ca::usz from) noexcept -> void {
		for (ca::usz i = from; i < b._size; ++i)
			b._data[i] = static_cast<char>(b._tag ^ static_cast<char>(i * 13U));
	}

	auto intact(const block& b) noexcept -> bool {
		for (ca::usz i = 0U; i < b._size; ++i)
			if (b._data[i] != static_cast<char>(b._tag ^ static_cast<char>(i * 13U)))
				return false;
		return true;
	}

	auto unmapped(const char* const at) noexcept -> bool {
		unsigned char resident = 0U;
		const ca::usz page = reinterpret_cast<ca::usz>(at) & ~ca::usz{4095U};
		return mincore(reinterpret_cast<void*>(page), 1U, &resident) != 0;
	}

	auto kill(block& b) noexcept -> void {
		b._alive = false;
		if (b._size >= 8U && (reinterpret_cast<ca::usz>(b._data) & 7U) == 0U)
			fuzz::require(ca::impl::is_poisoned(b._data) || unmapped(b._data), "released arena block not poisoned");
	}

	/* The newest block of level's lane, when it is of level and alive. */
	auto newest(model& m, const ca::usz level) noexcept -> block* {
		const ca::usz at = m._newest[level % 2U];
		if (at == NONE || !m._blocks[at]._alive || m._blocks[at]._level != level)
			return nullptr;
		return &m._blocks[at];
	}

	template <typename A>
	auto allocate(model& m, const A& allocator, const ca::usz level, fuzz::input& in) noexcept -> void {
		if (m._count == BLOCKS)
			return;
		const ca::usz size = in.take<ca::u8>() < 240U ? in.below(129U) : in.below(1U << 15U);
		const ca::usz alignment = ca::usz{1U} << in.below(8U);
		const auto made = allocator.allocate(size, alignment);
		if (!made)
			return;
		fuzz::require((reinterpret_cast<ca::usz>(made->data()) & (alignment - 1U)) == 0U && made->size() >= size, "arena block misaligned or short");
		block& b = m._blocks[m._count];
		b = block{static_cast<char*>(made->data()), made->size(), level, static_cast<char>(m._count + 1U), true};
		mark(b, 0U);
		m._newest[level % 2U] = m._count++;
	}

	template <typename A>
	auto expand(block* const b, const A& allocator, fuzz::input& in) noexcept -> void {
		if (b == nullptr)
			return;
		const auto expanded = allocator.expand(ca::allocation{b->_data, b->_size}, in.below(513U), 1U);
		if (!expanded)
			return;
		fuzz::require(expanded->data() == b->_data, "expand moved the block");
		const ca::usz kept = b->_size < expanded->size() ? b->_size : expanded->size();
		b->_size = expanded->size();
		mark(*b, kept);
	}

} // namespace

extern "C" auto LLVMFuzzerTestOneInput(const ca::u8* const data, const ca::usz size) -> int {

	auto made = ca::arena::make(1024U + size % 8192U);
	if (!made)
		return 0;
	ca::arena arena = ca::move(*made);
	static model m;
	m = model{};
	m._newest[0] = NONE;
	m._newest[1] = NONE;
	const ca::arena_scope first = arena.push();
	ca::arena_scope scopes[DEPTH + 1U] = {first, first, first, first, first, first, first, first, first};
	ca::usz depth = 0U;
	fuzz::input in{data, size};
	while (!in.empty()) {
		switch (in.take<ca::u8>() % 7U) {
		case 0:
			if (depth < DEPTH) {
				scopes[depth + 1U] = depth == 0U ? arena.push() : scopes[depth].push();
				++depth;
				m._newest[depth % 2U] = NONE;
			}
			break;
		case 1:
			if (depth != 0U) {
				scopes[depth].pop();
				for (ca::usz i = 0U; i < m._count; ++i)
					if (m._blocks[i]._alive && m._blocks[i]._level >= depth)
						kill(m._blocks[i]);
				m._newest[depth % 2U] = NONE;
				--depth;
			}
			break;
		case 2:
			if (depth == 0U)
				allocate(m, arena.allocator(), 0U, in);
			else
				allocate(m, scopes[depth], depth, in);
			break;
		case 3:
			if (depth == 0U)
				allocate(m, arena.allocator(), 0U, in);
			else
				allocate(m, scopes[depth].result(), depth - 1U, in);
			break;
		case 4:
			if (depth == 0U)
				expand(newest(m, 0U), arena.allocator(), in);
			else
				expand(newest(m, depth), scopes[depth], in);
			break;
		case 5:
			if (depth != 0U)
				expand(newest(m, depth - 1U), scopes[depth].result(), in);
			break;
		default: {
			block* const b = newest(m, depth);
			if (b == nullptr)
				break;
			if (depth == 0U)
				arena.allocator().deallocate(ca::allocation{b->_data, b->_size}, 1U);
			else
				scopes[depth].deallocate(ca::allocation{b->_data, b->_size}, 1U);
			kill(*b);
			m._newest[depth % 2U] = NONE;
			break;
		}
		}
		for (ca::usz i = 0U; i < m._count; ++i)
			if (m._blocks[i]._alive)
				fuzz::require(intact(m._blocks[i]), "live arena block overwritten");
	}
	while (depth != 0U)
		scopes[depth--].pop();
	fuzz::require(static_cast<bool>(arena.cleanup()), "arena cleanup failed");
	return 0;
}
