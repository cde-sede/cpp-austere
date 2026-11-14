// expect: exit 0
// link: both
// sanitize: !address
// The malloc family as the program sees it: libc's, or libca_runtime's on ca::heap when linked
// without libc. Sizes across the heap's classes and its large blocks, contents kept by realloc,
// calloc zeroed and refusing overflow, aligned blocks, free(nullptr). Not under ASan: its malloc
// is neither, and refuses aligned_alloc of a size no multiple of the alignment (C11).
#include "1-primitives/integers/u8.hpp"
#include "1-primitives/integers/usz.hpp"

extern "C" auto malloc(ca::usz size) noexcept -> void*;
extern "C" auto calloc(ca::usz count, ca::usz size) noexcept -> void*;
extern "C" auto realloc(void* block, ca::usz size) noexcept -> void*;
extern "C" auto free(void* block) noexcept -> void;
extern "C" auto aligned_alloc(ca::usz alignment, ca::usz size) noexcept -> void*;
extern "C" auto posix_memalign(void** out, ca::usz alignment, ca::usz size) noexcept -> int;

static volatile ca::usz huge = ca::usz{1} << 40U;

static auto fill(ca::u8* const block, const ca::usz size, const ca::u8 seed) -> void {
	for (ca::usz i = 0; i < size; ++i)
		block[i] = static_cast<ca::u8>(seed + i * 7U);
}

static auto holds(const ca::u8* const block, const ca::usz size, const ca::u8 seed) -> bool {
	for (ca::usz i = 0; i < size; ++i)
		if (block[i] != static_cast<ca::u8>(seed + i * 7U))
			return false;
	return true;
}

auto main(void) -> int {
	static const ca::usz sizes[] = {0U, 1U, 15U, 16U, 17U, 100U, 1000U, 4096U, 70000U, 200000U, 3000000U};
	for (const ca::usz size : sizes) {
		ca::u8* const block = static_cast<ca::u8*>(malloc(size));
		if (block == nullptr || reinterpret_cast<ca::usz>(block) % 16U != 0U)
			return 1;
		fill(block, size, 3U);
		if (!holds(block, size, 3U))
			return 2;
		free(block);
	}
	ca::u8* grown = static_cast<ca::u8*>(malloc(10U));
	fill(grown, 10U, 5U);
	for (ca::usz size = 10U; size < 1000000U; size = size * 3U + 1U) {
		ca::u8* const next = static_cast<ca::u8*>(realloc(grown, size * 3U + 1U));
		if (next == nullptr || !holds(next, size, 5U))
			return 3;
		grown = next;
		fill(grown, size * 3U + 1U, 5U);
	}
	ca::u8* const shrunk = static_cast<ca::u8*>(realloc(grown, 8U));
	if (shrunk == nullptr || !holds(shrunk, 8U, 5U))
		return 4;
	free(shrunk);
	ca::u8* const fresh = static_cast<ca::u8*>(realloc(nullptr, 32U));
	if (fresh == nullptr)
		return 5;
	free(fresh);
	ca::u8* const zeroed = static_cast<ca::u8*>(calloc(1000U, 33U));
	if (zeroed == nullptr)
		return 6;
	for (ca::usz i = 0; i < 33000U; ++i)
		if (zeroed[i] != 0U)
			return 7;
	free(zeroed);
	void* volatile overflowed = calloc(huge, huge);
	if (overflowed != nullptr)
		return 8;
	for (ca::usz alignment = 16U; alignment <= 8192U; alignment *= 2U) {
		void* const block = aligned_alloc(alignment, 100U);
		void* other = nullptr;
		if (block == nullptr || reinterpret_cast<ca::usz>(block) % alignment != 0U)
			return 9;
		if (posix_memalign(&other, alignment, 3U * alignment) != 0 || reinterpret_cast<ca::usz>(other) % alignment != 0U)
			return 10;
		fill(static_cast<ca::u8*>(other), 3U * alignment, 9U);
		free(block);
		free(other);
	}
	void* refused = nullptr;
	if (posix_memalign(&refused, 3U, 16U) == 0)
		return 11;
	free(nullptr);
	return 0;
}
