/* Process entry for programs linked without libc (linux x86-64, -static or -static-pie):
   _start, then the program's own relocations (static PIE), the main thread's TLS, the init
   arrays, main and exit.

   src/runtime/ is not part of libca.a: it builds libca_runtime.a, linked only by freestanding
   programs, because it defines the symbols libc's startup files and libc itself define (_start,
   exit, __cxa_atexit, __dso_handle, ...); in libca.a the linker would pull them into every
   program before reaching libc. Link line:

       c++ -nostdlib -static -no-pie prog.o -Wl,--start-group libca.a libca_runtime.a
           -Wl,--end-group -Wl,-u,_start

   1. _start (asm): the kernel leaves argc, argv, envp and the auxiliary vector on the stack;
      rdi = that stack pointer, the stack aligned to 16, then start(), which never returns. The
      auxiliary vector is kept in syscall::impl::auxiliary_vector, for syscall::auxiliary.
   2. TLS (variant II, laid out by ca::sys::impl, thread/impl/tls, which threads ca starts
      share): the PT_TLS image found and kept in sys::impl::program_tls, copied below the
      thread pointer in fresh pages, the control block above it (stack canary from AT_RANDOM),
      then arch_prctl(ARCH_SET_FS). Until then nothing may touch %fs: no
      thread_local, no ca::syscall call that fails (last_error is thread_local), no stack
      protector (no_stack_protector).
   3. preinit_array and init_array, each called (argc, argv, envp) as glibc does, so ca's own
      capture of the environment (3-system/environment) works in both kinds of programs.
   4. exit(main(argc, argv, envp)).
*/

#include "1-primitives/config/architecture.hpp"
#include "1-primitives/config/platform.hpp"
#include "1-primitives/integers/isz.hpp"
#include "1-primitives/integers/u64.hpp"
#include "1-primitives/integers/u8.hpp"
#include "1-primitives/integers/usz.hpp"
#include "3-system/elf/auxiliary_entry.hpp"
#include "3-system/elf/dynamic_entry.hpp"
#include "3-system/elf/file_header.hpp"
#include "3-system/elf/program_header.hpp"
#include "3-system/elf/relocation.hpp"
#include "3-system/elf/symbol.hpp"
#include "3-system/syscall/auxiliary.hpp"
#include "3-system/syscall/impl/enter.hpp"
#include "3-system/syscall/syscalls.hpp"
#include "3-system/thread/impl/tls.hpp"

#if ca_platform(linux) && ca_architecture(x86_64)

/* The program's main, by its symbol: main may not be declared extern "C". */
auto program_main(int argc, char** argv, char** envp) noexcept -> int asm("main"); // ns-leak: ignore

extern "C" [[noreturn]] auto exit(int status) noexcept -> void; // ns-leak: ignore

extern "C" char** environ; // ns-leak: ignore

using init_function = void (*)(int, char**, char**); // ns-leak: ignore

extern "C" [[gnu::visibility("hidden")]] init_function __preinit_array_start[]; // ns-leak: ignore
extern "C" [[gnu::visibility("hidden")]] init_function __preinit_array_end[]; // ns-leak: ignore
extern "C" [[gnu::visibility("hidden")]] init_function __init_array_start[]; // ns-leak: ignore
extern "C" [[gnu::visibility("hidden")]] init_function __init_array_end[]; // ns-leak: ignore

/* The dynamic section, only in a static PIE (weak: null in a static non-PIE program), and the
   ELF header, at the load address in a PIE. */
extern "C" [[gnu::visibility("hidden"), gnu::weak]] const ca::elf::dynamic_entry _DYNAMIC[]; // ns-leak: ignore
extern "C" [[gnu::visibility("hidden")]] const ca::elf::file_header __ehdr_start; // ns-leak: ignore

static constexpr ca::isz ARCH_SET_FS = 0x1002;

/* Raw syscall, no error handling: TLS may not exist yet. */
template <typename... As>
[[gnu::always_inline]] static inline auto raw(const ca::isz number, const As... arguments) noexcept -> ca::isz {
	return ca::syscall::impl::enter(number, ca::syscall::impl::word(arguments)...);
}

[[gnu::no_stack_protector]] static auto auxiliary(const ca::elf::auxiliary_entry* entry, const ca::u64 type) noexcept -> ca::u64 {

	for (; entry->_type != ca::elf::auxiliary_types::null; ++entry) {
		if (entry->_type == type)
			return entry->_value;
	}
	return 0U;
}

[[gnu::no_stack_protector]] static auto copy(char* to, const char* from, ca::usz count) noexcept -> void {

	for (; count != 0U; --count)
		*to++ = *from++;
	asm("" : : "r"(to) : "memory");
}

/* Bytes of the program's TLS block (0 without PT_TLS), from its program headers, which follow
   its ELF header in the first load segment. */
[[gnu::no_stack_protector]] static auto tls_size(const ca::u64 base) noexcept -> ca::u64 {

	const auto* const segments = reinterpret_cast<const ca::elf::program_header*>(base + __ehdr_start._segments_offset);
	for (ca::usz i = 0U; i < __ehdr_start._segment_count; ++i) {
		if (segments[i]._type == ca::elf::segment_types::tls)
			return ca::sys::impl::tls_block_size(segments[i]._memory_size, segments[i]._align);
	}
	return 0U;
}

/* A static PIE relocates itself before reading any global: R_X86_64_RELATIVE entries of DT_RELA,
   DT_RELR's packed ones (-z pack-relative-relocs), and R_X86_64_TPOFF64 ones, which a link
   exporting the program's symbols (--export-dynamic) leaves for its exported thread-local
   variables, made preemptible: their GOT slots get the variable's offset from the thread
   pointer, which only depends on the layout set_up_thread gives the main thread's block, and
   find_tls_image every other one. Locals and PC-relative addresses only, nothing else is in
   place yet: _DYNAMIC by lea in asm, since the compiler reads a weak symbol through the GOT,
   itself not yet relocated. */
[[gnu::no_stack_protector]] static auto relocate(void) noexcept -> void {

	const ca::elf::dynamic_entry* dynamic;
	asm(".weak _DYNAMIC\n\tleaq _DYNAMIC(%%rip), %0" : "=r"(dynamic));
	if (dynamic == nullptr)
		return;
	const ca::u64 base = reinterpret_cast<ca::u64>(&__ehdr_start);
	const ca::elf::relocation* rela = nullptr;
	ca::usz rela_size = 0U;
	const ca::u64* relr = nullptr;
	ca::usz relr_size = 0U;
	const ca::elf::symbol* symbols = nullptr;
	for (; dynamic->_tag != ca::elf::dynamic_tags::null; ++dynamic) {
		if (dynamic->_tag == ca::elf::dynamic_tags::rela)
			rela = reinterpret_cast<const ca::elf::relocation*>(base + dynamic->_value);
		else if (dynamic->_tag == ca::elf::dynamic_tags::rela_size)
			rela_size = dynamic->_value;
		else if (dynamic->_tag == ca::elf::dynamic_tags::relr)
			relr = reinterpret_cast<const ca::u64*>(base + dynamic->_value);
		else if (dynamic->_tag == ca::elf::dynamic_tags::relr_size)
			relr_size = dynamic->_value;
		else if (dynamic->_tag == ca::elf::dynamic_tags::symbols)
			symbols = reinterpret_cast<const ca::elf::symbol*>(base + dynamic->_value);
	}
	ca::u64 block = ~ca::u64{0U};
	for (ca::usz i = 0U; rela != nullptr && i < rela_size / sizeof(ca::elf::relocation); ++i) {
		const ca::u32 type = rela[i].type();
		ca::u64* const slot = reinterpret_cast<ca::u64*>(base + rela[i]._offset);
		if (type == ca::elf::x86_64_relocations::relative)
			*slot = base + static_cast<ca::u64>(rela[i]._addend);
		else if (type == ca::elf::x86_64_relocations::tls_thread_offset) {
			if (block == ~ca::u64{0U})
				block = tls_size(base);
			const ca::u64 offset = rela[i].symbol() != 0U && symbols != nullptr ? symbols[rela[i].symbol()]._value : 0U;
			*slot = offset + static_cast<ca::u64>(rela[i]._addend) - block;
		}
	}
	ca::u64* where = nullptr;
	for (ca::usz i = 0U; relr != nullptr && i < relr_size / sizeof(ca::u64); ++i) {
		ca::u64 entry = relr[i];
		if ((entry & 1U) == 0U) {
			where = reinterpret_cast<ca::u64*>(base + entry);
			*where++ += base;
			continue;
		}
		for (ca::usz bit = 0U; (entry >>= 1U) != 0U; ++bit) {
			if ((entry & 1U) != 0U)
				where[bit] += base;
		}
		where += 63;
	}
}

/* The main thread's TLS block and control block, then %fs. */
[[gnu::no_stack_protector]] static auto set_up_thread(const ca::elf::auxiliary_entry* const auxv) noexcept -> void {

	const ca::sys::impl::tls_image image = ca::sys::impl::find_tls_image(reinterpret_cast<const void*>(auxiliary(auxv, ca::elf::auxiliary_types::program_headers)),
	                                                                     auxiliary(auxv, ca::elf::auxiliary_types::program_header_count));
	ca::sys::impl::program_tls = image;
	const ca::usz length = (ca::sys::impl::tls_area_size(image) + 4095U) & ~ca::usz{4095U};
	const ca::isz block = raw(ca::syscall::number::mmap, 0, length, 3, 0x22, -1, 0);
	if (block < 0 && block > -4096) [[unlikely]]
		raw(ca::syscall::number::exit_group, 127);
	const ca::u8* const random = reinterpret_cast<const ca::u8*>(auxiliary(auxv, ca::elf::auxiliary_types::random));
	ca::u64 canary = 0U;
	if (random != nullptr)
		copy(reinterpret_cast<char*>(&canary), reinterpret_cast<const char*>(random), sizeof(canary));
	const ca::u64 pointer = ca::sys::impl::install_tls(image, reinterpret_cast<char*>(block), canary & ~ca::u64{0xff});
	raw(ca::syscall::number::arch_prctl, ARCH_SET_FS, pointer);
}

extern "C" [[noreturn, gnu::no_stack_protector, gnu::used]] auto ca_runtime_start(ca::u64* const stack) noexcept -> void { // ns-leak: ignore

	relocate();
	const int argc = static_cast<int>(stack[0]);
	char** const argv = reinterpret_cast<char**>(stack + 1);
	char** const envp = argv + argc + 1;
	char** end = envp;
	while (*end != nullptr)
		++end;
	environ = envp;
	const auto* const auxv = reinterpret_cast<const ca::elf::auxiliary_entry*>(end + 1);
	ca::syscall::impl::auxiliary_vector = auxv;
	set_up_thread(auxv);
	for (init_function* f = __preinit_array_start; f != __preinit_array_end; ++f)
		(*f)(argc, argv, envp);
	for (init_function* f = __init_array_start; f != __init_array_end; ++f)
		(*f)(argc, argv, envp);
	exit(program_main(argc, argv, envp));
}

asm(".text\n" // norm: ignore
	".globl _start\n"
	".type _start, @function\n"
	"_start:\n"
	"\txorl %ebp, %ebp\n"
	"\tmovq %rsp, %rdi\n"
	"\tandq $-16, %rsp\n"
	"\tcall ca_runtime_start\n"
	"\thlt\n");

#endif
