/* @DOC ca::dl::library: a shared object loaded by ca's own dynamic loader, owned until cleanup. */
#ifndef ca_resources_dl_library_hpp
#define ca_resources_dl_library_hpp

#include "1-primitives/debug/trap.hpp"
#include "1-primitives/utility/exchange.hpp"
#include "2-vocabulary/borrowed_slice.hpp"
#include "2-vocabulary/optional.hpp"
#include "2-vocabulary/result.hpp"
#include "2-vocabulary/sv/str.hpp"
#include "3-system/elf/image.hpp"
#include "5-collections/trace/report.hpp"
#include "6-resources/dl/host_symbol.hpp"
#include "6-resources/dl/impl/loader.hpp"
#include "6-resources/dl/impl/object.hpp"


// -- CA NAMESPACE -----------------------------------------------------------

namespace ca {


	// -- DL -----------------------------------------------------------------

	namespace dl {


		namespace impl {

			[[noreturn, gnu::cold, gnu::noinline]] inline auto assigned_over_open_library(void) noexcept -> void {
				ca::trap();
			}

		} // namespace impl


		// -- LIBRARY --------------------------------------------------------

		/* @DOC A reference to a shared object ca loaded and to everything it needs, as dlopen
		   gives (RTLD_NOW | RTLD_LOCAL): mapped, every relocation applied, initializers run.
		   Loading what is already loaded (same file, or a name matching a DT_SONAME) shares
		   it; the last cleanup() of a shared object runs its finalizers and unmaps it. ELF64
		   x86-64 shared objects on Linux, thread-local storage in the dynamic models. */
		class library final {


			private:

				// -- private types ------------------------------------------

				using self = ca::dl::library;


				// -- private members ----------------------------------------

				ca::dl::impl::object* _object;


				// -- private lifecycle --------------------------------------

				constexpr explicit library(ca::dl::impl::object* const object) noexcept
				: _object{object} {
				}


			public:

				// -- public lifecycle ---------------------------------------

				/* @DOC Owning nothing. */
				constexpr library(void) noexcept
				: _object{nullptr} {
				}

				library(const self&) = delete;

				constexpr library(self&& other) noexcept
				: _object{ca::exchange(other._object, nullptr)} {
				}

				/* @DOC Loads name: a path when it has a slash, else a file name searched for as
				   ld.so does (LD_LIBRARY_PATH unless the program is setuid or that cannot be told, then the default
				   directories; a dependency also looks in its requester's DT_RPATH and
				   DT_RUNPATH, $ORIGIN expanded; no ld.so.cache). Symbols a library leaves
				   undefined resolve to host first, then to what the running program exports
				   (its dynamic symbol table: link it with --export-dynamic; a static non-PIE
				   program has none), then to the first definition in the library and what
				   it needs, breadth first; an undefined weak symbol is 0. A thread-local
				   variable only the program defines is refused (errc::not_supported).
				   The report names the symbol or file that failed. Initializers and
				   finalizers must not load or release libraries. */
				[[nodiscard]] static auto make(const ca::sv::str name, const ca::borrowed_slice<const ca::dl::host_symbol> host = {}) noexcept -> ca::result<self, ca::report<>> {

					auto loaded = ca::dl::impl::load(name, host);
					if (!loaded)
						return ca::failure{ca::move(loaded).error()};
					return self{*loaded};
				}

				/* @DOC make, named for what it does. */
				[[nodiscard]] static auto open(const ca::sv::str name, const ca::borrowed_slice<const ca::dl::host_symbol> host = {}) noexcept -> ca::result<self, ca::report<>> {
					return self::make(name, host);
				}


				// -- assignment operators -----------------------------------

				auto operator=(const self&) -> self& = delete;

				/* @DOC Takes other's library. Traps when this one is still open: cleanup() first. */
				auto operator=(self&& other) noexcept -> self& {

					if (this == &other)
						return *this;
					if (_object != nullptr) [[unlikely]]
						ca::dl::impl::assigned_over_open_library();
					_object = ca::exchange(other._object, nullptr);
					return *this;
				}


				// -- accessors ----------------------------------------------

				[[nodiscard]] constexpr auto is_open(void) const noexcept -> bool {
					return _object != nullptr;
				}

				/* @DOC The file it was loaded from; empty when not open. */
				[[nodiscard]] auto path(void) const noexcept -> ca::sv::str {
					return _object != nullptr ? _object->_path.view() : ca::sv::str{};
				}

				/* @DOC Its DT_SONAME; empty without one or when not open. */
				[[nodiscard]] auto soname(void) const noexcept -> ca::sv::str {
					return _object != nullptr ? _object->soname() : ca::sv::str{};
				}

				/* @DOC The mapped image, for its segments and dynamic section; empty when not
				   open. */
				[[nodiscard]] auto image(void) const noexcept -> ca::elf::image {
					return _object != nullptr ? _object->_image : ca::elf::image{};
				}


				// -- methods ------------------------------------------------

				/* @DOC The address of the symbol named name in the library or what it needs,
				   breadth first, as dlsym: an indirect function resolved, a thread-local
				   variable the calling thread's copy. None when it is undefined or the
				   library is not open. */
				[[nodiscard]] auto find(const ca::sv::str name) const noexcept -> ca::optional<void*> {

					if (_object == nullptr)
						return ca::optional<void*>{};
					return ca::dl::impl::lookup(*_object, name);
				}

				/* @DOC find, as a pointer to T: a function type (`find_as<int(int)>("f")`) or an
				   object type. */
				template <typename T>
				[[nodiscard]] auto find_as(const ca::sv::str name) const noexcept -> ca::optional<T*> {

					const auto found = this->find(name);
					if (!found)
						return ca::optional<T*>{};
					return ca::optional<T*>{reinterpret_cast<T*>(*found)};
				}

				/* @DOC Gives the reference back: the last one to a shared object runs its
				   finalizers (DT_FINI_ARRAY in reverse, then DT_FINI), releases what it needs
				   and unmaps it. Not open afterwards, whatever the result. */
				[[nodiscard]] auto cleanup(void) noexcept -> ca::result<void> {

					if (_object == nullptr)
						return ca::result<void>{};
					return ca::dl::impl::release(ca::exchange(_object, nullptr));
				}

		}; // class library

	} // namespace dl

} // namespace ca

#endif // ca_resources_dl_library_hpp
