#include "1-primitives/atomic/memory_order.hpp"
#include "1-primitives/integers/byte.hpp"
#include "1-primitives/integers/i64.hpp"
#include "1-primitives/integers/u32.hpp"
#include "1-primitives/integers/u64.hpp"
#include "1-primitives/integers/usz.hpp"
#include "1-primitives/memory/fill_bytes.hpp"
#include "1-primitives/utility/move.hpp"
#include "2-vocabulary/borrowed_slice.hpp"
#include "2-vocabulary/failure.hpp"
#include "2-vocabulary/optional.hpp"
#include "2-vocabulary/result.hpp"
#include "2-vocabulary/span.hpp"
#include "2-vocabulary/sv/str.hpp"
#include "3-system/elf/auxiliary_entry.hpp"
#include "3-system/elf/dynamic_entry.hpp"
#include "3-system/elf/file_header.hpp"
#include "3-system/elf/hashed_name.hpp"
#include "3-system/elf/image.hpp"
#include "3-system/elf/program_header.hpp"
#include "3-system/elf/relocation.hpp"
#include "3-system/elf/section_header.hpp"
#include "3-system/elf/symbol.hpp"
#include "3-system/environment/environ.hpp"
#include "3-system/environment/getenv.hpp"
#include "3-system/error/errc.hpp"
#include "3-system/file/fstat.hpp"
#include "3-system/file/pread.hpp"
#include "3-system/memory/mmap_anonymous.hpp"
#include "3-system/memory/mmap_anonymous_fixed.hpp"
#include "3-system/memory/mmap_file_fixed.hpp"
#include "3-system/memory/mprotect.hpp"
#include "3-system/memory/munmap.hpp"
#include "3-system/memory/page_size.hpp"
#include "3-system/memory/protection.hpp"
#include "3-system/sync/mutex.hpp"
#include "3-system/syscall/auxiliary.hpp"
#include "4-lifecycle/unique.hpp"
#include "5-collections/array/dynamic_array.hpp"
#include "5-collections/string/str.hpp"
#include "5-collections/trace/context.hpp"
#include "5-collections/trace/report.hpp"
#include "6-resources/dl/impl/loader.hpp"
#include "6-resources/dl/impl/tls.hpp"
#include "6-resources/fs/descriptor.hpp"
#include "6-resources/trace/impl/loaded_objects.hpp"


namespace {

	using object = ca::dl::impl::object;

	using init_function     = void (*)(int, char**, char**);
	using fini_function     = void (*)(void);
	using resolver_function = ca::u64 (*)(void);

	constinit ca::mutex registry_lock{};
	constinit ca::dynamic_array<ca::unique<object>> registry{};

	/* The running program, read on the first load, under registry_lock; empty when it cannot
	   be found. */
	constinit ca::elf::image program_image{};
	constinit bool program_read = false;

	constexpr ca::sv::str DEFAULT_DIRECTORIES[] = {
		"/lib/x86_64-linux-gnu", "/usr/lib/x86_64-linux-gnu", "/lib64", "/usr/lib64", "/lib", "/usr/lib",
	};

	/* What one load call works on: the host's symbols, the running program, and the objects it
	   mapped, in the order it mapped them (breadth first from the root). */
	struct session final {

			using self = session;

			ca::borrowed_slice<const ca::dl::host_symbol> _host;
			const ca::elf::image* _program;
			ca::dynamic_array<object*> _fresh;

	}; // struct session


	// -- TEXT ---------------------------------------------------------------

	auto has_slash(const ca::sv::str text) noexcept -> bool {

		for (const char c : text) {
			if (c == '/')
				return true;
		}
		return false;
	}

	auto directory_of(const ca::sv::str path) noexcept -> ca::sv::str {

		for (ca::usz i = path.size(); i > 0U; --i) {
			if (path.data()[i - 1U] == '/')
				return ca::sv::str{path.data(), i > 1U ? i - 1U : 1U};
		}
		return ca::sv::str{"."};
	}

	/* path_in(directory, name), $ORIGIN or ${ORIGIN} at the start of directory meaning origin. */
	[[nodiscard]] auto path_in(const ca::sv::str directory, const ca::sv::str name, const ca::sv::str origin) noexcept -> ca::result<ca::str> {

		ca::sv::str rest = directory;
		ca::sv::str head{};
		if (directory.starts_with(ca::sv::str{"${ORIGIN}"})) {
			head = origin;
			rest = ca::sv::str{directory.data() + 9U, directory.size() - 9U};
		}
		else if (directory.starts_with(ca::sv::str{"$ORIGIN"})) {
			head = origin;
			rest = ca::sv::str{directory.data() + 7U, directory.size() - 7U};
		}
		auto path = ca::str::make(head);
		if (!path)
			return path;
		auto appended = path->append(rest);
		if (appended)
			appended = path->append(ca::sv::str{"/"});
		if (appended)
			appended = path->append(name);
		if (!appended) {
			static_cast<void>(path->cleanup());
			return appended.propagate();
		}
		return path;
	}


	// -- PAGES --------------------------------------------------------------

	auto page_floor(const ca::u64 value, const ca::u64 page) noexcept -> ca::u64 {
		return value & ~(page - 1U);
	}

	auto page_ceiling(const ca::u64 value, const ca::u64 page) noexcept -> ca::u64 {
		return (value + page - 1U) & ~(page - 1U);
	}

	auto region(const ca::u64 begin, const ca::u64 end) noexcept -> ca::span<ca::byte> {
		return ca::span<ca::byte>{reinterpret_cast<ca::byte*>(begin), static_cast<ca::usz>(end - begin)};
	}

	/* Whether size bytes at address lie inside the mapping of o. */
	auto inside_mapping(const object& o, const ca::u64 address, const ca::u64 size) noexcept -> bool {

		const ca::u64 begin = reinterpret_cast<ca::u64>(o._mapping.data());
		return address >= begin && size <= o._mapping.size() && address - begin <= o._mapping.size() - size;
	}

	auto protection_of(const ca::u32 flags) noexcept -> int {

		int protection = ca::sys::protection::none;
		if ((flags & ca::elf::segment_flags::read) != 0U)
			protection |= ca::sys::protection::read;
		if ((flags & ca::elf::segment_flags::write) != 0U)
			protection |= ca::sys::protection::write;
		if ((flags & ca::elf::segment_flags::execute) != 0U)
			protection |= ca::sys::protection::execute;
		return protection;
	}

	[[nodiscard]] auto read_exact(const int fd, const ca::span<ca::byte> into, const ca::u64 offset) noexcept -> ca::result<bool> {

		ca::usz done = 0U;
		while (done < into.size()) {
			auto read = ca::sys::pread(fd, ca::span<ca::byte>{into.data() + done, into.size() - done}, offset + done);
			if (!read)
				return read.propagate();
			if (*read == 0U)
				return false;
			done += *read;
		}
		return true;
	}


	// -- MAPPING ------------------------------------------------------------

	/* The load segments of o mapped from fd into one reservation aligned for the largest
	   alignment they ask for; o._segments read already. */
	[[nodiscard]] auto map_segments(object& o, const int fd, const ca::u64 file_size) noexcept -> ca::result<void, ca::report<>> {

		const ca::u64 page = ca::sys::page_size();
		ca::u64 low = ~ca::u64{0U};
		ca::u64 high = 0U;
		ca::u64 align = page;
		for (const ca::elf::program_header& segment : o._segments) {
			if (segment._type != ca::elf::segment_types::load || segment._memory_size == 0U)
				continue;
			if (segment._file_size > segment._memory_size || segment._address + segment._memory_size < segment._address
			 || segment._offset + segment._file_size < segment._offset || segment._offset + segment._file_size > file_size
			 || (segment._address - segment._offset) % page != 0U)
				return ca::context(ca::errc::invalid_argument, "{}: malformed load segment", o._path.view());
			if (segment._align > align && (segment._align & (segment._align - 1U)) == 0U)
				align = segment._align;
			if (segment._address < low)
				low = segment._address;
			if (segment._address + segment._memory_size > high)
				high = segment._address + segment._memory_size;
		}
		if (high == 0U)
			return ca::context(ca::errc::invalid_argument, "{}: no load segment", o._path.view());
		  low = page_floor(low, page);
		 high = page_ceiling(high, page);
		const ca::u64 size = high - low;
		auto reserved = ca::sys::mmap_anonymous(static_cast<ca::usz>(size + align - page), ca::sys::protection::none);
		if (!reserved)
			return ca::context(reserved.error(), "{}: reserving {} bytes", o._path.view(), size + align - page);
		const ca::u64 start = reinterpret_cast<ca::u64>(reserved->data());
		const ca::u64 base = (start + align - 1U) & ~(align - 1U);
		const ca::u64 finish = start + reserved->size();
		if (base > start)
			static_cast<void>(ca::sys::munmap(region(start, base)));
		if (finish > base + size)
			static_cast<void>(ca::sys::munmap(region(base + size, finish)));
		o._mapping = region(base, base + size);
		const ca::u64 bias = base - low;
		for (const ca::elf::program_header& segment : o._segments) {
			if (segment._type != ca::elf::segment_types::load || segment._memory_size == 0U)
				continue;
			const int protection = protection_of(segment._flags);
			const ca::u64 begin = bias + page_floor(segment._address, page);
			const ca::u64 data_end = bias + segment._address + segment._file_size;
			const ca::u64 end = page_ceiling(bias + segment._address + segment._memory_size, page);
			ca::u64 zeroes = begin;
			if (segment._file_size != 0U) {
				auto mapped = ca::sys::mmap_file_fixed(region(begin, page_ceiling(data_end, page)), fd, page_floor(segment._offset, page), protection);
				if (!mapped) {
					static_cast<void>(ca::sys::munmap(o._mapping));
					return ca::context(mapped.error(), "{}: mapping a segment", o._path.view());
				}
				zeroes = page_ceiling(data_end, page);
				if (segment._memory_size > segment._file_size && data_end < zeroes) {
					if ((protection & ca::sys::protection::write) == 0) {
						static_cast<void>(ca::sys::munmap(o._mapping));
						return ca::context(ca::errc::invalid_argument, "{}: read-only segment with zero fill", o._path.view());
					}
					ca::fill_bytes(reinterpret_cast<void*>(data_end), 0U, static_cast<ca::usz>(zeroes - data_end));
				}
			}
			if (end > zeroes) {
				auto mapped = ca::sys::mmap_anonymous_fixed(region(zeroes, end), protection);
				if (!mapped) {
					static_cast<void>(ca::sys::munmap(o._mapping));
					return ca::context(mapped.error(), "{}: mapping zero pages", o._path.view());
				}
			}
		}
		o._image = ca::elf::image::of(o._segments.view(), bias);
		const auto flags = o._image.value(ca::elf::dynamic_tags::flags);
		if (o._image.value(ca::elf::dynamic_tags::text_relocations) || (flags && (*flags & ca::elf::dynamic_flags::text_relocations) != 0U)) {
			static_cast<void>(ca::sys::munmap(o._mapping));
			return ca::context(ca::errc::not_supported, "{}: relocations in read-only segments are not supported", o._path.view());
		}
		if (flags && (*flags & ca::elf::dynamic_flags::static_tls) != 0U) {
			static_cast<void>(ca::sys::munmap(o._mapping));
			return ca::context(ca::errc::not_supported, "{}: initial-exec thread-local storage (DF_STATIC_TLS) is not supported", o._path.view());
		}
		const auto tls = o._image.segment(ca::elf::segment_types::tls);
		if (tls) {
			if (tls->_file_size > tls->_memory_size || !inside_mapping(o, o._image.address(tls->_address), tls->_file_size)
			 || (tls->_align & (tls->_align - 1U)) != 0U) {
				static_cast<void>(ca::sys::munmap(o._mapping));
				return ca::context(ca::errc::invalid_argument, "{}: malformed thread-local storage segment", o._path.view());
			}
			auto module = ca::dl::impl::tls_register(reinterpret_cast<const ca::byte*>(o._image.address(tls->_address)), static_cast<ca::usz>(tls->_file_size),
			                                         static_cast<ca::usz>(tls->_memory_size), static_cast<ca::usz>(tls->_align));
			if (!module) {
				static_cast<void>(ca::sys::munmap(o._mapping));
				return ca::context(module.error(), "{}: registering its thread-local storage", o._path.view());
			}
			o._tls_module = *module;
		}
		return ca::result<void, ca::report<>>{};
	}

	/* A new object for the shared object open at fd, mapped; its path taken. */
	[[nodiscard]] auto map_object(ca::str&& path, const int fd, const ca::elf::file_header& header, const ca::u64 device, const ca::u64 inode, const ca::u64 file_size) noexcept
	-> ca::result<ca::unique<object>, ca::report<>> {

		auto made = ca::unique<object>::make();
		if (!made) {
			auto failed = ca::context(made.error(), "loading {}", path.view());
			static_cast<void>(path.cleanup());
			return failed;
		}
		object& o = **made;
		       o._path = ca::move(path);
		     o._device = device;
		      o._inode = inode;
		 o._tls_module = 0U;
		 o._references = 1U;
		  o._relocated = false;
		o._initialized = false;
		const auto drop = [&](ca::unique<object>& doomed) noexcept -> void {
			static_cast<void>(doomed.cleanup());
		};
		if (header._segment_count == 0U || header._segments_offset > file_size) {
			auto failed = ca::context(ca::errc::invalid_argument, "{}: no program headers", o._path.view());
			drop(*made);
			return failed;
		}
		auto sized = o._segments.resize(header._segment_count);
		if (!sized) {
			auto failed = ca::context(sized.error(), "{}: reading program headers", o._path.view());
			drop(*made);
			return failed;
		}
		auto read = read_exact(fd, ca::span<ca::byte>{reinterpret_cast<ca::byte*>(o._segments.data()), o._segments.size() * sizeof(ca::elf::program_header)}, header._segments_offset);
		if (!read || !*read) {
			auto failed = ca::context(read ? ca::errc::invalid_argument : read.error(), "{}: reading program headers", o._path.view());
			drop(*made);
			return failed;
		}
		auto mapped = map_segments(o, fd, file_size);
		if (!mapped) {
			auto failed = ca::failure{ca::move(mapped).error()};
			drop(*made);
			return failed;
		}
		return ca::result<ca::unique<object>, ca::report<>>{ca::move(*made)};
	}


	// -- REGISTRY -----------------------------------------------------------

	auto loaded_as(const ca::sv::str soname) noexcept -> object* {

		for (ca::unique<object>& o : registry) {
			if (o->soname() == soname)
				return o.get();
		}
		return nullptr;
	}

	auto loaded_from(const ca::u64 device, const ca::u64 inode) noexcept -> object* {

		for (ca::unique<object>& o : registry) {
			if (o->_device == device && o->_inode == inode)
				return o.get();
		}
		return nullptr;
	}

	/* The object of the file at path: one already loaded from it (one more reference), or a new
	   one; none when path is not a usable x86-64 shared object and explicit is false (a search
	   goes on), an error when it is true. */
	[[nodiscard]] auto acquire_file(ca::str&& path, const bool explicit_path, session& s) noexcept -> ca::result<object*, ca::report<>> {

		auto file = ca::fs::descriptor::open(path.c_str(), ca::fs::readable | ca::fs::cloexec);
		ca::error error = ca::errc::not_supported;
		ca::sv::str why{"not an ELF64 x86-64 shared object"};
		if (file) {
			auto found = [&](void) noexcept -> ca::optional<ca::result<object*, ca::report<>>> {
				using outcome = ca::optional<ca::result<object*, ca::report<>>>;
				auto status = ca::sys::fstat(file->native());
				if (!status) {
					error = status.error();
					  why = ca::sv::str{"cannot stat"};
					return outcome{};
				}
				if (object* const known = loaded_from(status->device(), status->inode())) {
					++known->_references;
					static_cast<void>(path.cleanup());
					return outcome{ca::result<object*, ca::report<>>{known}};
				}
				ca::elf::file_header header{};
				auto read = read_exact(file->native(), ca::span<ca::byte>{reinterpret_cast<ca::byte*>(&header), sizeof(header)}, 0U);
				if (!read) {
					error = read.error();
					  why = ca::sv::str{"cannot read"};
					return outcome{};
				}
				if (!*read || !header.is_elf64() || header._machine != ca::elf::machines::x86_64 || header._type != ca::elf::file_types::shared)
					return outcome{};
				auto reserved = registry.reserve(registry.size() + 1U);
				if (reserved)
					reserved = s._fresh.reserve(s._fresh.size() + 1U);
				if (!reserved) {
					auto failed = ca::context(reserved.error(), "loading {}", path.view());
					static_cast<void>(path.cleanup());
					return outcome{ca::result<object*, ca::report<>>{ca::move(failed)}};
				}
				auto made = map_object(ca::move(path), file->native(), header, status->device(), status->inode(), status->size());
				if (!made)
					return outcome{ca::result<object*, ca::report<>>{ca::failure{ca::move(made).error()}}};
				object* const o = made->get();
				static_cast<void>(registry.append(ca::move(*made)));
				static_cast<void>(s._fresh.append(o));
				return outcome{ca::result<object*, ca::report<>>{o}};
			}();
			static_cast<void>(file->cleanup());
			if (found)
				return ca::move(*found);
		}
		else {
			error = file.error();
			  why = ca::sv::str{"cannot open"};
		}
		if (!explicit_path) {
			static_cast<void>(path.cleanup());
			return ca::result<object*, ca::report<>>{nullptr};
		}
		auto failed = ca::context(error, "{}: {}", path.view(), why);
		static_cast<void>(path.cleanup());
		return failed;
	}

	/* Tries name in each directory of list (colon-separated). */
	[[nodiscard]] auto search_in(const ca::sv::str list, const ca::sv::str name, const ca::sv::str origin, session& s) noexcept -> ca::result<object*, ca::report<>> {

		ca::usz begin = 0U;
		while (begin <= list.size()) {
			ca::usz end = begin;
			while (end < list.size() && list.data()[end] != ':')
				++end;
			const ca::sv::str directory{list.data() + begin, end - begin};
			begin = end + 1U;
			if (directory.empty())
				continue;
			auto path = path_in(directory, name, origin);
			if (!path)
				return ca::context(path.error(), "searching for {}", name);
			auto found = acquire_file(ca::move(*path), false, s);
			if (!found || *found != nullptr)
				return found;
		}
		return ca::result<object*, ca::report<>>{nullptr};
	}

	auto search_list(const object* const requester, const ca::i64 tag) noexcept -> ca::sv::str {

		if (requester == nullptr)
			return ca::sv::str{};
		const auto offset = requester->_image.value(tag);
		return offset ? requester->_image.string(*offset) : ca::sv::str{};
	}

	/* The object for name, needed by requester (null for the root): one already loaded under
	   that soname, the file at name when it has a slash, else the first usable file found in
	   requester's DT_RPATH (without DT_RUNPATH), LD_LIBRARY_PATH (unless secure or secure unknown), DT_RUNPATH,
	   then the default directories. */
	[[nodiscard]] auto acquire(const ca::sv::str name, const object* const requester, session& s) noexcept -> ca::result<object*, ca::report<>> {

		if (has_slash(name)) {
			auto path = ca::str::make(name);
			if (!path)
				return ca::context(path.error(), "loading {}", name);
			return acquire_file(ca::move(*path), true, s);
		}
		if (object* const known = loaded_as(name)) {
			++known->_references;
			return ca::result<object*, ca::report<>>{known};
		}
		const ca::sv::str origin = requester != nullptr ? directory_of(requester->_path.view()) : ca::sv::str{"."};
		const ca::sv::str runpath = search_list(requester, ca::elf::dynamic_tags::runpath);
		ca::sv::str lists[4] = {};
		if (runpath.empty())
			lists[0] = search_list(requester, ca::elf::dynamic_tags::rpath);
		if (ca::syscall::auxiliary(ca::elf::auxiliary_types::secure).value_or(1U) == 0U) {
			const auto environment = ca::sys::getenv("LD_LIBRARY_PATH");
			if (environment)
				lists[1] = *environment;
		}
		lists[2] = runpath;
		for (const ca::sv::str list : lists) {
			auto found = search_in(list, name, origin, s);
			if (!found || *found != nullptr)
				return found;
		}
		for (const ca::sv::str directory : DEFAULT_DIRECTORIES) {
			auto found = search_in(directory, name, origin, s);
			if (!found || *found != nullptr)
				return found;
		}
		return ca::context(ca::errc::no_entry, "{}: not found", name);
	}


	// -- SYMBOLS ------------------------------------------------------------

	/* What a reference resolved to: a symbol and the object defining it, or, without a symbol,
	   an address (a host symbol, a built-in, 0 for a missing weak symbol). */
	struct resolved final {

			using self = resolved;

			const object* _owner;
			const ca::elf::symbol* _symbol;
			ca::u64 _address;

	}; // struct resolved

	/* Where symbol, defined by image and not thread-local, is: its value plus image's bias (none
	   for an absolute symbol), an indirect function's resolver called. */
	[[nodiscard]] auto definition(const ca::elf::image& image, const ca::elf::symbol& symbol) noexcept -> ca::u64 {

		const ca::u64 address = symbol._section == ca::elf::section_indices::absolute ? symbol._value : image.address(symbol._value);
		if (symbol.type() == ca::elf::symbol_types::gnu_ifunc)
			return reinterpret_cast<resolver_function>(address)();
		return address;
	}

	/* Symbols the loader itself provides, as ld.so does. */
	[[nodiscard]] auto built_in(const ca::sv::str name) noexcept -> ca::optional<ca::u64> {

		if (name == ca::sv::str{"__tls_get_addr"})
			return ca::optional<ca::u64>{reinterpret_cast<ca::u64>(&ca::dl::impl::tls_get_addr)};
		return ca::optional<ca::u64>{};
	}

	auto find_in(const ca::dynamic_array<object*>& scope, const ca::elf::hashed_name& name, const object*& owner) noexcept -> const ca::elf::symbol* {

		for (const object* const o : scope) {
			const auto found = o->_image.find(name);
			if (found && found->binding() != ca::elf::symbol_bindings::local) {
				owner = o;
				return &*found;
			}
		}
		return nullptr;
	}

	auto is_symbolic(const object& o) noexcept -> bool {

		const auto flags = o._image.value(ca::elf::dynamic_tags::flags);
		return o._image.value(ca::elf::dynamic_tags::symbolic) || (flags && (*flags & ca::elf::dynamic_flags::symbolic) != 0U);
	}

	/* What symbol index of o refers to: o's own for a local symbol; else the host's, then the
	   loader's built-ins (before the program's: an ASan program exports its own
	   __tls_get_addr, blind to these modules), then o's own under DT_SYMBOLIC, then the
	   program's exported ones (a thread-local one only when nothing else defines it, and then
	   refused), then the first definition in root's scope; address 0 for an undefined weak
	   symbol. */
	[[nodiscard]] auto resolve(const object& o, const ca::u32 index, const object& root, const session& s) noexcept -> ca::result<resolved, ca::report<>> {

		const auto symbols = o._image.symbols();
		if (index >= symbols.size())
			return ca::context(ca::errc::invalid_argument, "{}: symbol index {} out of range", o._path.view(), index);
		const ca::elf::symbol& symbol = symbols[index];
		const ca::sv::str name = o._image.string(symbol._name);
		const object* owner = &o;
		const ca::elf::symbol* found = nullptr;
		bool program_tls = false;
		if (symbol.binding() == ca::elf::symbol_bindings::local)
			found = symbol.is_defined() ? &symbol : nullptr;
		else {
			for (const ca::dl::host_symbol& offered : s._host) {
				if (offered._name == name)
					return resolved{nullptr, nullptr, reinterpret_cast<ca::u64>(offered._address)};
			}
			const auto provided = built_in(name);
			if (provided)
				return resolved{nullptr, nullptr, *provided};
			const ca::elf::hashed_name hashed = ca::elf::hashed_name::of(name);
			if (is_symbolic(o)) {
				const auto own = o._image.find(hashed);
				if (own)
					found = &*own;
			}
			if (found == nullptr) {
				const auto exported = s._program->find(hashed);
				if (exported && exported->binding() != ca::elf::symbol_bindings::local) {
					if (exported->type() != ca::elf::symbol_types::tls)
						return resolved{nullptr, nullptr, definition(*s._program, *exported)};
					program_tls = true;
				}
				found = find_in(root._scope, hashed, owner);
			}
		}
		if (found == nullptr && program_tls)
			return ca::context(ca::errc::not_supported, "{}: thread-local {} is the program's, which the loader cannot reach", o._path.view(), name);
		if (found == nullptr) {
			if (symbol.binding() == ca::elf::symbol_bindings::weak)
				return resolved{nullptr, nullptr, 0U};
			return ca::context(ca::errc::no_entry, "undefined symbol {}, needed by {}", name, o._path.view());
		}
		return resolved{owner, found, 0U};
	}

	/* The address a reference to symbol index of o stands for. */
	[[nodiscard]] auto resolve_address(const object& o, const ca::u32 index, const object& root, const session& s) noexcept -> ca::result<ca::u64, ca::report<>> {

		auto target = resolve(o, index, root, s);
		if (!target)
			return ca::failure{ca::move(target).error()};
		if (target->_symbol == nullptr)
			return target->_address;
		if (target->_symbol->type() == ca::elf::symbol_types::tls)
			return ca::context(ca::errc::invalid_argument, "{}: thread-local {} used as an address", o._path.view(), target->_owner->_image.string(target->_symbol->_name));
		return definition(target->_owner->_image, *target->_symbol);
	}

	/* The thread-local symbol index of o refers to (none for index 0: o's own block), checked. */
	[[nodiscard]] auto resolve_tls(const object& o, const ca::u32 index, const object& root, const session& s) noexcept -> ca::result<resolved, ca::report<>> {

		if (index == 0U)
			return resolved{&o, nullptr, 0U};
		auto target = resolve(o, index, root, s);
		if (!target)
			return target;
		if (target->_symbol == nullptr || target->_symbol->type() != ca::elf::symbol_types::tls || target->_owner->_tls_module == 0U)
			return ca::context(ca::errc::invalid_argument, "{}: {} is not thread-local", o._path.view(), o._image.string(o._image.symbols()[index]._name));
		return target;
	}


	// -- RELOCATION ---------------------------------------------------------

	auto inside(const object& o, const ca::u64 address) noexcept -> bool {
		return inside_mapping(o, address, sizeof(ca::u64)) && address % alignof(ca::u64) == 0U;
	}

	/* The relative relocations of o: R_X86_64_RELATIVE entries, then DT_RELR. */
	[[nodiscard]] auto relocate_relative(object& o) noexcept -> ca::result<void, ca::report<>> {

		const ca::u64 bias = o._image.bias();
		for (const auto table : {o._image.relocations(), o._image.plt_relocations()}) {
			for (const ca::elf::relocation& r : table) {
				if (r.type() != ca::elf::x86_64_relocations::relative)
					continue;
				const ca::u64 where = o._image.address(r._offset);
				if (!inside(o, where))
					return ca::context(ca::errc::invalid_argument, "{}: relocation outside the object", o._path.view());
				*reinterpret_cast<ca::u64*>(where) = bias + static_cast<ca::u64>(r._addend);
			}
		}
		ca::u64 where = 0U;
		for (const ca::u64 entry : o._image.relative_relocations()) {
			if ((entry & 1U) == 0U) {
				where = o._image.address(entry);
				if (!inside(o, where))
					return ca::context(ca::errc::invalid_argument, "{}: relocation outside the object", o._path.view());
				*reinterpret_cast<ca::u64*>(where) += bias;
				where += sizeof(ca::u64);
				continue;
			}
			ca::u64 bits = entry;
			for (ca::u64 at = where; (bits >>= 1U) != 0U; at += sizeof(ca::u64)) {
				if ((bits & 1U) == 0U)
					continue;
				if (!inside(o, at))
					return ca::context(ca::errc::invalid_argument, "{}: relocation outside the object", o._path.view());
				*reinterpret_cast<ca::u64*>(at) += bias;
			}
			where += 63U * sizeof(ca::u64);
		}
		return ca::result<void, ca::report<>>{};
	}

	/* The relocations of o naming symbols, then its indirect relative ones, whose resolvers may
	   read anything relocated before. */
	[[nodiscard]] auto relocate_symbolic(object& o, const object& root, const session& s) noexcept -> ca::result<void, ca::report<>> {

		for (const bool indirect : {false, true}) {
			for (const auto table : {o._image.relocations(), o._image.plt_relocations()}) {
				for (const ca::elf::relocation& r : table) {
					const ca::u32 type = r.type();
					if (type == ca::elf::x86_64_relocations::none || type == ca::elf::x86_64_relocations::relative)
						continue;
					if ((type == ca::elf::x86_64_relocations::indirect_relative) != indirect)
						continue;
					const ca::u64 where = o._image.address(r._offset);
					if (!inside(o, where))
						return ca::context(ca::errc::invalid_argument, "{}: relocation outside the object", o._path.view());
					ca::u64 value = 0U;
					switch (type) {
						case ca::elf::x86_64_relocations::indirect_relative:
							value = reinterpret_cast<resolver_function>(o._image.bias() + static_cast<ca::u64>(r._addend))();
							break;
						case ca::elf::x86_64_relocations::absolute:
						case ca::elf::x86_64_relocations::global_data:
						case ca::elf::x86_64_relocations::jump_slot: {
							auto address = resolve_address(o, r.symbol(), root, s);
							if (!address)
								return ca::failure{ca::move(address).error()};
							value = *address + static_cast<ca::u64>(r._addend);
							break;
						}
						case ca::elf::x86_64_relocations::tls_module: {
							auto target = resolve_tls(o, r.symbol(), root, s);
							if (!target)
								return ca::failure{ca::move(target).error()};
							value = target->_owner->_tls_module;
							if (value == 0U)
								return ca::context(ca::errc::invalid_argument, "{}: thread-local reference without a thread-local segment", o._path.view());
							break;
						}
						case ca::elf::x86_64_relocations::tls_offset: {
							auto target = resolve_tls(o, r.symbol(), root, s);
							if (!target)
								return ca::failure{ca::move(target).error()};
							value = (target->_symbol != nullptr ? target->_symbol->_value : 0U) + static_cast<ca::u64>(r._addend);
							break;
						}
						case ca::elf::x86_64_relocations::copy:
							return ca::context(ca::errc::invalid_argument, "{}: copy relocation in a shared object", o._path.view());
						case ca::elf::x86_64_relocations::tls_thread_offset:
							return ca::context(ca::errc::not_supported, "{}: initial-exec thread-local storage (R_X86_64_TPOFF64) is not supported", o._path.view());
						case ca::elf::x86_64_relocations::tls_descriptor:
							return ca::context(ca::errc::not_supported, "{}: TLS descriptors (-mtls-dialect=gnu2) are not supported yet", o._path.view());
						default:
							return ca::context(ca::errc::not_supported, "{}: relocation type {} is not supported", o._path.view(), type);
					}
					*reinterpret_cast<ca::u64*>(where) = value;
				}
			}
		}
		return ca::result<void, ca::report<>>{};
	}

	/* Makes PT_GNU_RELRO read-only, whole pages only: the last partial page stays writable. */
	[[nodiscard]] auto protect_relro(object& o) noexcept -> ca::result<void, ca::report<>> {

		const ca::u64 page = ca::sys::page_size();
		const auto relro = o._image.segment(ca::elf::segment_types::gnu_relro);
		if (!relro)
			return ca::result<void, ca::report<>>{};
		const ca::u64 begin = page_floor(o._image.address(relro->_address), page);
		const ca::u64 end = page_floor(o._image.address(relro->_address + relro->_memory_size), page);
		if (end <= begin)
			return ca::result<void, ca::report<>>{};
		auto protect = ca::sys::mprotect(region(begin, end), ca::sys::protection::read);
		if (!protect)
			return ca::context(protect.error(), "{}: protecting relocated read-only data", o._path.view());
		return ca::result<void, ca::report<>>{};
	}


	// -- LIFECYCLE ----------------------------------------------------------

	[[nodiscard]] auto build_scope(object& o) noexcept -> ca::result<void> {

		auto first = o._scope.append(&o);
		if (!first)
			return first;
		for (ca::usz i = 0U; i < o._scope.size(); ++i) {
			for (object* const needed : o._scope.data()[i]->_needed) {
				if (o._scope.view().contains(needed))
					continue;
				auto added = o._scope.append(needed);
				if (!added)
					return added;
			}
		}
		return ca::result<void>{};
	}

	/* Runs the initializers of what o needs, then o's own (DT_INIT, then DT_INIT_ARRAY), once. */
	auto initialize(object& o) noexcept -> void {

		if (o._initialized)
			return;
		o._initialized = true;
		for (object* const needed : o._needed)
			initialize(*needed);
		char** const environment = const_cast<char**>(ca::sys::environ());
		const auto init = o._image.address_of(ca::elf::dynamic_tags::init);
		if (init)
			reinterpret_cast<init_function>(*init)(0, nullptr, environment);
		for (const ca::u64 function : o._image.words(ca::elf::dynamic_tags::init_array, ca::elf::dynamic_tags::init_array_size)) {
			if (function != 0U && function != ~ca::u64{0U})
				reinterpret_cast<init_function>(function)(0, nullptr, environment);
		}
	}

	auto finalize(object& o) noexcept -> void {

		const auto finis = o._image.words(ca::elf::dynamic_tags::fini_array, ca::elf::dynamic_tags::fini_array_size);
		for (ca::usz i = finis.size(); i > 0U; --i) {
			if (finis[i - 1U] != 0U && finis[i - 1U] != ~ca::u64{0U})
				reinterpret_cast<fini_function>(finis[i - 1U])();
		}
		const auto fini = o._image.address_of(ca::elf::dynamic_tags::fini);
		if (fini)
			reinterpret_cast<fini_function>(*fini)();
	}

	[[nodiscard]] auto release_locked(object* const o) noexcept -> ca::result<void> {

		if (--o->_references != 0U)
			return ca::result<void>{};
		if (o->_initialized)
			finalize(*o);
		ca::result<void> outcome{};
		for (object* const needed : o->_needed) {
			auto released = release_locked(needed);
			if (outcome && !released)
				outcome = released;
		}
		ca::dl::impl::tls_unregister(o->_tls_module);
		auto unmapped = ca::sys::munmap(o->_mapping);
		if (outcome && !unmapped)
			outcome = unmapped;
		for (ca::usz i = 0U; i < registry.size(); ++i) {
			if (registry.data()[i].get() != o)
				continue;
			auto removed = registry.pop(i);
			if (removed) {
				auto cleaned = removed->cleanup();
				if (outcome && !cleaned)
					outcome = cleaned;
			}
			break;
		}
		return outcome;
	}

	[[nodiscard]] auto load_locked(const ca::sv::str name, session& s) noexcept -> ca::result<object*, ca::report<>> {

		auto root = acquire(name, nullptr, s);
		if (!root)
			return root;
		const auto fail = [&](ca::failure<ca::report<>>&& failed) noexcept -> ca::result<object*, ca::report<>> {
			static_cast<void>(release_locked(*root));
			return ca::move(failed);
		};
		for (ca::usz i = 0U; i < s._fresh.size(); ++i) {
			object* const o = s._fresh.data()[i];
			for (const ca::elf::dynamic_entry& entry : o->_image.dynamic()) {
				if (entry._tag != ca::elf::dynamic_tags::needed)
					continue;
				const ca::sv::str needed_name = o->_image.string(entry._value);
				auto needed = acquire(needed_name, o, s);
				if (!needed)
					return fail(ca::context(ca::move(needed).error(), "needed by {}", o->_path.view()));
				auto kept = o->_needed.append(*needed);
				if (!kept) {
					static_cast<void>(release_locked(*needed));
					return fail(ca::context(kept.error(), "loading {}", needed_name));
				}
			}
		}
		for (object* const o : s._fresh) {
			auto scoped = build_scope(*o);
			if (!scoped)
				return fail(ca::context(scoped.error(), "loading {}", name));
		}
		for (object* const o : s._fresh) {
			auto relative = relocate_relative(*o);
			if (!relative)
				return fail(ca::failure{ca::move(relative).error()});
		}
		for (ca::usz i = s._fresh.size(); i > 0U; --i) {
			object& o = *s._fresh.data()[i - 1U];
			auto symbolic = relocate_symbolic(o, **root, s);
			if (!symbolic)
				return fail(ca::failure{ca::move(symbolic).error()});
			auto relro = protect_relro(o);
			if (!relro)
				return fail(ca::failure{ca::move(relro).error()});
			o._relocated = true;
		}
		initialize(**root);
		return root;
	}

	/* The running program's image, read once; under registry_lock. */
	auto program_locked(void) noexcept -> const ca::elf::image& {

		if (!program_read) {
			const auto found = ca::elf::image::program();
			if (found)
				program_image = *found;
			program_read = true;
		}
		return program_image;
	}

	/* Visits every object loaded, for a symbolizer (ca::trace::impl::loaded_objects). */
	auto walk_objects(const ca::trace::impl::loaded_object_visitor visit, void* const context) noexcept -> void {

		registry_lock.lock();
		for (const ca::unique<object>& o : registry) {
			const ca::usz begin = reinterpret_cast<ca::usz>(o->_mapping.data());
			visit(context, ca::trace::impl::loaded_object{o->_path.view(), o->_device, o->_inode, static_cast<ca::usz>(o->_image.bias()), begin, begin + o->_mapping.size()});
		}
		registry_lock.unlock();
	}

} // namespace


[[nodiscard]] auto ca::dl::impl::load(const ca::sv::str name, const ca::borrowed_slice<const ca::dl::host_symbol> host) noexcept -> ca::result<ca::dl::impl::object*, ca::report<>> {

	session s{host, nullptr, {}};
	ca::trace::impl::loaded_objects.store<ca::memory_order::release>(walk_objects);
	registry_lock.lock();
	s._program = &program_locked();
	auto loaded = load_locked(name, s);
	registry_lock.unlock();
	static_cast<void>(s._fresh.cleanup());
	return loaded;
}

[[nodiscard]] auto ca::dl::impl::release(ca::dl::impl::object* const object) noexcept -> ca::result<void> {

	registry_lock.lock();
	auto released = release_locked(object);
	registry_lock.unlock();
	return released;
}

[[nodiscard]] auto ca::dl::impl::lookup(const ca::dl::impl::object& object, const ca::sv::str name) noexcept -> ca::optional<void*> {

	const ca::elf::hashed_name hashed = ca::elf::hashed_name::of(name);
	const ca::dl::impl::object* owner = nullptr;
	const ca::elf::symbol* const found = find_in(object._scope, hashed, owner);
	if (found == nullptr)
		return ca::optional<void*>{};
	if (found->type() == ca::elf::symbol_types::tls) {
		const ca::dl::impl::tls_index index{owner->_tls_module, found->_value};
		if (index._module == 0U)
			return ca::optional<void*>{};
		return ca::optional<void*>{ca::dl::impl::tls_get_addr(&index)};
	}
	return ca::optional<void*>{reinterpret_cast<void*>(definition(owner->_image, *found))};
}
