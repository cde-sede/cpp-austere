#include "1-primitives/config/architecture.hpp"
#include "1-primitives/config/platform.hpp"
#include "2-vocabulary/failure.hpp"
#include "3-system/error/last_error.hpp"
#include "3-system/memory/shared_segment.hpp"
#include "3-system/syscall/syscalls.hpp"

#include <sys/ipc.h>
#include <sys/shm.h>


namespace {

	/* IPC_PRIVATE, IPC_CREAT and IPC_RMID, checked below against <sys/ipc.h>. */
	constexpr int PRIVATE = 0;
	constexpr int CREATE = 01000;
	constexpr int REMOVE = 0;
	constexpr int OWNER_READ_WRITE = 0600;

} // namespace

static_assert(IPC_PRIVATE == PRIVATE);
static_assert(IPC_CREAT == CREATE);
static_assert(IPC_RMID == REMOVE);


[[nodiscard]] auto ca::sys::shm_create(const ca::usz size) noexcept -> ca::result<int> {

#if ca_platform(linux) && ca_architecture(x86_64)
	const auto made = ca::syscall::shmget(PRIVATE, size, CREATE | OWNER_READ_WRITE);
	if (!made) [[unlikely]]
		return made.propagate();
	return static_cast<int>(*made);
#else
	const int id = ::shmget(IPC_PRIVATE, size, IPC_CREAT | OWNER_READ_WRITE);
	if (id == -1) [[unlikely]]
		return ca::failure{ca::sys::impl::libc_error()};
	return id;
#endif
}

[[nodiscard]] auto ca::sys::shm_attach(const int id, const ca::usz size) noexcept -> ca::result<ca::span<ca::byte>> {

#if ca_platform(linux) && ca_architecture(x86_64)
	const auto mapped = ca::syscall::shmat(id, 0, 0);
	if (!mapped) [[unlikely]]
		return mapped.propagate();
	return ca::span<ca::byte>{reinterpret_cast<ca::byte*>(*mapped), size};
#else
	void* const mapped = ::shmat(id, nullptr, 0);
	if (mapped == reinterpret_cast<void*>(-1)) [[unlikely]]
		return ca::failure{ca::sys::impl::libc_error()};
	return ca::span<ca::byte>{static_cast<ca::byte*>(mapped), size};
#endif
}

[[nodiscard]] auto ca::sys::shm_detach(const ca::span<ca::byte> mapping) noexcept -> ca::result<void> {

#if ca_platform(linux) && ca_architecture(x86_64)
	const auto done = ca::syscall::shmdt(mapping.data());
	if (!done) [[unlikely]]
		return done.propagate();
	return ca::result<void>{};
#else
	if (::shmdt(mapping.data()) == -1) [[unlikely]]
		return ca::failure{ca::sys::impl::libc_error()};
	return ca::result<void>{};
#endif
}

[[nodiscard]] auto ca::sys::shm_remove(const int id) noexcept -> ca::result<void> {

#if ca_platform(linux) && ca_architecture(x86_64)
	const auto done = ca::syscall::shmctl(id, REMOVE, 0);
	if (!done) [[unlikely]]
		return done.propagate();
	return ca::result<void>{};
#else
	if (::shmctl(id, IPC_RMID, nullptr) == -1) [[unlikely]]
		return ca::failure{ca::sys::impl::libc_error()};
	return ca::result<void>{};
#endif
}
