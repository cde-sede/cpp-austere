/* What every fuzz target shares: the libFuzzer entry point, a reader taking numbers and bytes
   from the front of the input, and require, which stops the run on a broken invariant so that
   libFuzzer keeps the input (build/fuzz/crashes/NAME). Targets are linked against libc (the
   reference implementations they compare ca to) and built with ASan and UBSan; see
   `make fuzz`. */

#ifndef ca_fuzz_fuzz_hpp
#define ca_fuzz_fuzz_hpp

#include "1-primitives/integers/byte.hpp"
#include "1-primitives/integers/u32.hpp"
#include "1-primitives/integers/u8.hpp"
#include "1-primitives/integers/usz.hpp"
#include "1-primitives/memory/copy_bytes.hpp"
#include "1-primitives/memory/string_length.hpp"
#include "2-vocabulary/span.hpp"
#include "2-vocabulary/sv/str.hpp"
#include "3-system/file/write_all.hpp"

extern "C" auto LLVMFuzzerTestOneInput(const ca::u8* data, ca::usz size) -> int;

namespace fuzz {

	/* The input, consumed from the front. Past its end numbers are 0 and views empty. */
	class input final {

		private:

			const ca::u8* _data;
			ca::usz _size;

		public:

			input(const ca::u8* const data, const ca::usz size) noexcept
			: _data{data}, _size{size} {
			}

			[[nodiscard]] auto empty(void) const noexcept -> bool {
				return _size == 0U;
			}

			[[nodiscard]] auto left(void) const noexcept -> ca::usz {
				return _size;
			}

			/* @DOC The next sizeof(T) bytes as a T, zero-extended past the end. */
			template <typename T>
			[[nodiscard]] auto take(void) noexcept -> T {
				ca::u8 bytes[sizeof(T)] = {};
				const ca::usz count = _size < sizeof(T) ? _size : sizeof(T);
				ca::copy_bytes(bytes, _data, count);
				_data += count;
				_size -= count;
				return __builtin_bit_cast(T, bytes);
			}

			/* @DOC A number below bound (0 when bound is 0). */
			[[nodiscard]] auto below(const ca::usz bound) noexcept -> ca::usz {
				return bound == 0U ? 0U : this->take<ca::u32>() % bound;
			}

			/* @DOC Up to count bytes. */
			[[nodiscard]] auto bytes(const ca::usz count) noexcept -> ca::span<const ca::u8> {
				const ca::usz taken = _size < count ? _size : count;
				const ca::span<const ca::u8> out{_data, taken};
				_data += taken;
				_size -= taken;
				return out;
			}

			/* @DOC A view of bytes, its length taken first (below limit). */
			[[nodiscard]] auto text(const ca::usz limit) noexcept -> ca::sv::str {
				const ca::span<const ca::u8> taken = this->bytes(this->below(limit));
				return ca::sv::str{reinterpret_cast<const char*>(taken.data()), taken.size()};
			}

			/* @DOC Everything left. */
			[[nodiscard]] auto rest(void) noexcept -> ca::span<const ca::u8> {
				return this->bytes(_size);
			}

	}; // class input

	/* @DOC Stops the run when holds is false, naming what broke. */
	inline auto require(const bool holds, const char* const what) noexcept -> void {
		if (holds) [[likely]]
			return;
		static_cast<void>(ca::sys::write_all(2, ca::span<const ca::byte>{reinterpret_cast<const ca::byte*>("fuzz: broken: "), 14U}));
		static_cast<void>(ca::sys::write_all(2, ca::span<const ca::byte>{reinterpret_cast<const ca::byte*>(what), ca::string_length(what)}));
		static_cast<void>(ca::sys::write_all(2, ca::span<const ca::byte>{reinterpret_cast<const ca::byte*>("\n"), 1U}));
		__builtin_trap();
	}

} // namespace fuzz

#endif // ca_fuzz_fuzz_hpp
