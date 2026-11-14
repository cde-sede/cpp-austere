// expect: exit 0
// sanitize: !address
// ASan puts its own memcpy and memmove in front of ca's.
#include "1-primitives/memory/copy_bytes.hpp"
#include "1-primitives/memory/impl/move_bytes.hpp"

extern "C" auto memcpy(void* to, const void* from, ca::usz count) noexcept -> void*;
extern "C" auto memmove(void* to, const void* from, ca::usz count) noexcept -> void*;

static volatile int calls = 0;
static ca::impl::move_bytes_function real = nullptr;

static auto counting(void* const to, const void* const from, const ca::usz count) noexcept -> void* {
	calls = calls + 1;
	return real(to, from, count);
}

static auto (*volatile copy_c)(void*, const void*, ca::usz) noexcept -> void* = &memcpy;
static auto (*volatile move_c)(void*, const void*, ca::usz) noexcept -> void* = &memmove;
static volatile ca::usz runtime = 100U;
static volatile ca::usz small = 64U;

auto main(void) -> int {
	char from[256];
	char to[256];
	for (int i = 0; i < 256; ++i)
		from[i] = static_cast<char>(i);
	ca::copy_bytes(to, from, runtime);
	real = ca::impl::move_bytes_variant.load();
	ca::impl::move_bytes_variant.store(&counting);
	if (copy_c(to, from, runtime) != to || calls != 1)
		return 1;
	if (move_c(from + 1, from, runtime) != from + 1 || calls != 2)
		return 2;
	__builtin_memcpy(to, from, runtime);
	if (copy_c(to, from, small) != to || move_c(to + 1, to, small) != to + 1 || calls != 3)
		return 5;
	ca::impl::move_bytes_variant.store(real);
	if (calls != 3)
		return 3;
	copy_c(to, from, runtime);
	for (ca::usz i = 0; i < runtime; ++i)
		if (to[i] != static_cast<char>(i == 0U ? 0 : i - 1U))
			return 4;
	return 0;
}
