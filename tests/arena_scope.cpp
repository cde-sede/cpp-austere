// expect: exit 0
#include "4-lifecycle/make_in.hpp"
#include "4-lifecycle/memory/allocator_of.hpp"
#include "4-lifecycle/memory/arena.hpp"
#include "4-lifecycle/memory/expand.hpp"
#include "4-lifecycle/memory/is_stateful_allocator.hpp"
#include "5-collections/array/dynamic_array.hpp"

static_assert(ca::is_stateful_allocator<ca::arena_scope>);
static_assert(ca::is_same<ca::allocator_of<ca::arena_scope>, ca::arena_scope>);
static_assert(sizeof(ca::arena_scope) == 2U * sizeof(void*));
static_assert(sizeof(ca::arena_allocator) == sizeof(void*));

using kept_array = ca::dynamic_array<int, ca::arena_allocator>;

static auto at(const ca::result<ca::allocation>& block) noexcept -> char* {
	return static_cast<char*>(block->data());
}

static auto fill(char* const data, const ca::usz size, const char value) noexcept -> void {
	for (ca::usz i = 0U; i < size; ++i)
		data[i] = value;
}

static auto filled(const char* const data, const ca::usz size, const char value) noexcept -> bool {
	for (ca::usz i = 0U; i < size; ++i)
		if (data[i] != value)
			return false;
	return true;
}

static auto rewinds(ca::arena& arena) noexcept -> bool {

	const ca::arena_scope scope = arena.push();
	auto first = scope.allocate(64U, 16U);
	if (!first)
		return false;
	for (int i = 0; i < 100; ++i)
		if (!scope.allocate(1000U, 8U))
			return false;
	scope.pop();
	const ca::arena_scope again = arena.push();
	auto second = again.allocate(64U, 16U);
	const bool same = second && second->data() == first->data();
	again.pop();
	return same;
}

static auto keeps_results(ca::arena& arena) noexcept -> bool {

	auto before = arena.allocator().allocate(32U, 8U);
	if (!before)
		return false;
	fill(at(before), 32U, 'b');
	const ca::arena_scope scope = arena.push();
	auto result = scope.result().allocate(5000U, 8U);
	if (!result)
		return false;
	fill(at(result), 5000U, 'r');
	for (int i = 0; i < 20; ++i) {
		auto temporary = scope.allocate(3000U, 8U);
		if (!temporary)
			return false;
		fill(at(temporary), 3000U, 't');
	}
	scope.pop();
	const ca::arena_scope next = arena.push();
	for (int i = 0; i < 20; ++i) {
		auto temporary = next.allocate(3000U, 8U);
		if (!temporary)
			return false;
		fill(at(temporary), 3000U, 'u');
	}
	next.pop();
	auto after = arena.allocator().allocate(8U, 8U);
	return after && at(after) >= at(result) + 5000 && filled(at(before), 32U, 'b') && filled(at(result), 5000U, 'r');
}

static auto nests(ca::arena& arena) noexcept -> bool {

	const ca::arena_scope outer = arena.push();
	auto outer_temporary = outer.allocate(100U, 8U);
	if (!outer_temporary)
		return false;
	fill(at(outer_temporary), 100U, 'o');
	const ca::arena_scope middle = outer.push();
	auto middle_result = middle.result().allocate(200U, 8U);
	if (!middle_result)
		return false;
	fill(at(middle_result), 200U, 'm');
	const ca::arena_scope inner = middle.push();
	auto inner_result = inner.result().allocate(300U, 8U);
	if (!inner_result)
		return false;
	fill(at(inner_result), 300U, 'i');
	for (int i = 0; i < 50; ++i)
		if (!inner.allocate(2000U, 8U))
			return false;
	inner.pop();
	if (!filled(at(inner_result), 300U, 'i'))
		return false;
	for (int i = 0; i < 50; ++i)
		if (!middle.allocate(2000U, 8U))
			return false;
	middle.pop();
	if (!filled(at(middle_result), 200U, 'm') || !filled(at(outer_temporary), 100U, 'o'))
		return false;
	auto reused = outer.push().allocate(8U, 8U);
	const bool inner_result_gone = reused && at(reused) == at(inner_result);
	outer.pop();
	return inner_result_gone;
}

static auto builds_results(ca::arena& arena) noexcept -> bool {

	const ca::arena_scope scope = arena.push();
	auto squares = ca::make_in<ca::dynamic_array<int>>(scope);
	if (!squares)
		return false;
	for (int i = 0; i < 5000; ++i)
		if (!squares->append(i * i))
			return false;
	auto kept = ca::make_in<ca::dynamic_array<kept_array>>(scope.result());
	if (!kept)
		return false;
	for (int row = 0; row < 10; ++row) {
		kept_array values{scope.result()};
		for (int i = 0; i < 100; ++i)
			if (!values.append((*squares)[row * 100 + i]))
				return false;
		if (!kept->append(ca::move(values)))
			return false;
	}
	scope.pop();
	const ca::arena_scope clobber = arena.push();
	for (int i = 0; i < 200; ++i) {
		auto temporary = clobber.allocate(1000U, 8U);
		if (!temporary)
			return false;
		fill(at(temporary), 1000U, 'x');
	}
	clobber.pop();
	for (int row = 0; row < 10; ++row)
		for (int i = 0; i < 100; ++i)
			if ((*kept)[static_cast<ca::usz>(row)][static_cast<ca::usz>(i)] != (row * 100 + i) * (row * 100 + i))
				return false;
	return true;
}

static auto grows_in_place(ca::arena& arena) noexcept -> bool {

	const ca::arena_scope scope = arena.push();
	auto block = scope.allocate(16U, 16U);
	if (!block)
		return false;
	auto grown = ca::expand(scope, *block, 512U, 16U);
	const bool in_place = grown && grown->data() == block->data();
	scope.deallocate(*grown, 16U);
	auto again = scope.allocate(16U, 16U);
	const bool given_back = again && again->data() == block->data();
	scope.pop();
	return in_place && given_back;
}

static auto cycles_across_chunks(ca::arena& arena) noexcept -> bool {

	const ca::arena_scope outer = arena.push();
	for (int round = 0; round < 1000; ++round) {
		const ca::arena_scope scope = outer.push();
		for (int i = 0; i < 6; ++i)
			if (!scope.allocate(1000U, 8U))
				return false;
		scope.pop();
	}
	outer.pop();
	return true;
}

auto main(void) -> int {

	auto made = ca::arena::make(4096U);
	if (!made)
		return 1;
	ca::arena arena = ca::move(*made);
	if (!rewinds(arena))
		return 2;
	if (!keeps_results(arena))
		return 3;
	if (!nests(arena))
		return 4;
	if (!builds_results(arena))
		return 5;
	if (!grows_in_place(arena))
		return 6;
	if (!cycles_across_chunks(arena))
		return 7;
	ca::arena moved = ca::move(arena);
	const ca::arena_scope scope = moved.push();
	if (!scope.allocate(100000U, 8U))
		return 8;
	scope.pop();
	moved.reset();
	if (!rewinds(moved))
		return 9;
	return moved.cleanup() ? 0 : 10;
}
