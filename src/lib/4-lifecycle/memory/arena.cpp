#include "1-primitives/debug/trap.hpp"
#include "1-primitives/memory/construct_at.hpp"
#include "1-primitives/memory/poison.hpp"
#include "1-primitives/numeric/add_overflow.hpp"
#include "1-primitives/numeric/align_up.hpp"
#include "1-primitives/numeric/max.hpp"
#include "2-vocabulary/failure.hpp"
#include "3-system/error/errc.hpp"
#include "4-lifecycle/memory/arena.hpp"
#include "4-lifecycle/memory/page_allocator.hpp"


static constexpr ca::usz chunk_alignment = ca::page_allocator::ALIGNMENT;

static auto unmap(ca::impl::arena_chunk* const chunk) noexcept -> void {

	ca::impl::unpoison_bytes(chunk, chunk->_size);
	ca::page_allocator{}.deallocate(ca::allocation{chunk, chunk->_size}, chunk_alignment);
}

static auto poison_body(ca::impl::arena_chunk* const chunk) noexcept -> void {
	ca::impl::poison_bytes(chunk + 1, chunk->_size - sizeof(ca::impl::arena_chunk));
}

static auto unmap_until(ca::impl::arena_chunk* chunk, ca::impl::arena_chunk* const last) noexcept -> void {
	while (chunk != last) {
		ca::impl::arena_chunk* const previous = chunk->_previous;
		unmap(chunk);
		chunk = previous;
	}
}

static auto release_spare(ca::impl::arena_lane& lane) noexcept -> void {
	if (lane._spare != nullptr)
		unmap(ca::exchange(lane._spare, nullptr));
}

static auto retire(ca::impl::arena_lane& lane, ca::impl::arena_chunk* const chunk) noexcept -> void {
	if (chunk->_size != lane._chunk_size)
		unmap(chunk);
	else {
		release_spare(lane);
		poison_body(chunk);
		lane._spare = chunk;
	}
}

static auto holds(const ca::impl::arena_chunk* const chunk, const char* const mark) noexcept -> bool {
	const char* const base = reinterpret_cast<const char*>(chunk);
	return mark > base && mark <= base + chunk->_size;
}

[[noreturn, gnu::noinline, gnu::cold]] auto ca::impl::allocator_of_an_empty_arena(void) noexcept -> void {
	ca::trap();
}

[[noreturn, gnu::noinline, gnu::cold]] auto ca::impl::pop_of_a_stale_scope(void) noexcept -> void {
	ca::trap();
}

auto ca::impl::arena_release(ca::impl::arena_state* const state) noexcept -> void {

	if (state == nullptr)
		return;
	release_spare(state->_lanes[1]);
	unmap_until(state->_lanes[1]._chunk, nullptr);
	release_spare(state->_lanes[0]);
	unmap_until(state->_lanes[0]._chunk, nullptr);
}

[[nodiscard, gnu::noinline]] auto ca::impl::arena_grow(ca::impl::arena_lane* const lane, const ca::usz size, const ca::usz alignment) noexcept -> ca::result<ca::allocation> {

	ca::usz bytes{0U};
	if (ca::add_overflow(bytes, size, alignment, sizeof(ca::impl::arena_chunk))) [[unlikely]]
		return ca::failure{ca::errc::out_of_memory};
	ca::allocation block{nullptr, 0U};
	if (lane->_spare != nullptr && bytes <= lane->_spare->_size) {
		const ca::usz spare_size = lane->_spare->_size;
		block = ca::allocation{ca::exchange(lane->_spare, nullptr), spare_size};
	}
	else {
		auto mapped = ca::page_allocator{}.allocate(ca::max(bytes, lane->_chunk_size), chunk_alignment);
		if (!mapped) [[unlikely]]
			return mapped.propagate();
		block = *mapped;
	}
	ca::impl::arena_chunk* const chunk = ca::construct_at(static_cast<ca::impl::arena_chunk*>(block.data()), ca::impl::arena_chunk{lane->_chunk, block.size()});
	ca::usz start = reinterpret_cast<ca::usz>(chunk + 1);
	static_cast<void>(ca::align_up(start, alignment));
	lane->_chunk = chunk;
	  lane->_top = reinterpret_cast<char*>(start + size);
	  lane->_end = reinterpret_cast<char*>(chunk) + chunk->_size;
	poison_body(chunk);
	ca::impl::unpoison_bytes(reinterpret_cast<void*>(start), size);
	return ca::allocation{reinterpret_cast<void*>(start), size};
}

auto ca::impl::arena_pop(ca::impl::arena_lane* const lane, char* const mark) noexcept -> void {

	ca::impl::arena_chunk* chunk = nullptr;
	if (mark != nullptr) {
		chunk = lane->_chunk;
		while (chunk != nullptr && !holds(chunk, mark))
			chunk = chunk->_previous;
		if (chunk == nullptr || (chunk == lane->_chunk && mark > lane->_top)) [[unlikely]]
			ca::impl::pop_of_a_stale_scope();
	}
	ca::impl::arena_chunk* newest = lane->_chunk;
	while (newest != chunk) {
		ca::impl::arena_chunk* const previous = newest->_previous;
		retire(*lane, newest);
		newest = previous;
	}
	lane->_chunk = chunk;
	  lane->_top = mark;
	  lane->_end = chunk == nullptr ? nullptr : reinterpret_cast<char*>(chunk) + chunk->_size;
	if (chunk != nullptr)
		ca::impl::poison_bytes(mark, static_cast<ca::usz>(lane->_end - mark));
}

[[nodiscard]] auto ca::arena::make(const ca::usz chunk_size) noexcept -> ca::result<self> {

	constexpr ca::usz header = sizeof(ca::impl::arena_chunk) + sizeof(ca::impl::arena_state);
	auto mapped = ca::page_allocator{}.allocate(ca::max(chunk_size, header + 1U), chunk_alignment);
	if (!mapped) [[unlikely]]
		return mapped.propagate();
	char* const base = static_cast<char*>(mapped->data());
	ca::impl::arena_chunk* const chunk = ca::construct_at(reinterpret_cast<ca::impl::arena_chunk*>(base), ca::impl::arena_chunk{nullptr, mapped->size()});
	ca::impl::arena_state* const state = ca::construct_at(reinterpret_cast<ca::impl::arena_state*>(base + sizeof(ca::impl::arena_chunk)), ca::impl::arena_state{
		{ca::impl::arena_lane{base + header, base + mapped->size(), chunk, nullptr, nullptr, mapped->size()},
		 ca::impl::arena_lane{nullptr, nullptr, nullptr, nullptr, nullptr, mapped->size()}},
		chunk});
	state->_lanes[0]._other = &state->_lanes[1];
	state->_lanes[1]._other = &state->_lanes[0];
	ca::impl::poison_bytes(base + header, mapped->size() - header);
	self built{};
	built._state = state;
	return built;
}

auto ca::arena::reset(void) noexcept -> void {

	if (_state == nullptr)
		return;
	ca::impl::arena_lane& first = _state->_lanes[0];
	ca::impl::arena_lane& second = _state->_lanes[1];
	release_spare(first);
	release_spare(second);
	unmap_until(first._chunk, _state->_first);
	unmap_until(second._chunk, nullptr);
	char* const base = reinterpret_cast<char*>(_state->_first);
	 first._chunk = _state->_first;
	   first._top = base + sizeof(ca::impl::arena_chunk) + sizeof(ca::impl::arena_state);
	   first._end = base + _state->_first->_size;
	ca::impl::poison_bytes(first._top, static_cast<ca::usz>(first._end - first._top));
	second._chunk = nullptr;
	  second._top = nullptr;
	  second._end = nullptr;
}
