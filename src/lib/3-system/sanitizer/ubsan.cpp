/* The handlers -fsanitize=undefined calls, for programs linked without UBSan's runtime (make
   SAN=undefined, and every freestanding program): each check that fails is a ca trap. A context
   taking it fails with the check as its reason, at the line of the source that failed; with
   none, the program writes `ca: undefined behavior: <check> at <file>:<line>:<column>` to stderr
   and stops by an illegal instruction. Weak: a sanitizer runtime linked in (ASan's carries
   UBSan's) keeps its own. Built with -fno-sanitize-recover the compiler calls the _abort names;
   both are here, and neither returns. */

#include "1-primitives/config/sanitizer.hpp"
#include "1-primitives/debug/frame_address.hpp"
#include "1-primitives/debug/recovery.hpp"
#include "1-primitives/debug/source_location.hpp"
#include "1-primitives/integers/byte.hpp"
#include "1-primitives/integers/u32.hpp"
#include "1-primitives/integers/u8.hpp"
#include "1-primitives/integers/usz.hpp"
#include "2-vocabulary/span.hpp"
#include "3-system/file/write_all.hpp"


namespace {

	/* What the data of every check starts with. */
	struct ubsan_location final {
			using self = ubsan_location;

			const char* _file;
			ca::u32 _line;
			ca::u32 _column;
	}; // struct ubsan_location

	/* The data of a type_mismatch_v1 check. */
	struct ubsan_type_mismatch final {
			using self = ubsan_type_mismatch;

			ubsan_location _where;
			const void* _type;
			ca::u8 _log_alignment;
			ca::u8 _kind;
	}; // struct ubsan_type_mismatch

	struct message final {
			using self = message;

			ca::byte _bytes[512];
			ca::usz _size;
	}; // struct message

	[[ca_no_sanitize(undefined)]] auto append(message& m, const char* text) noexcept -> void {

		if (text == nullptr)
			text = "?";
		while (*text != '\0' && m._size < sizeof(m._bytes))
			m._bytes[m._size++] = static_cast<ca::byte>(*text++);
	}

	[[ca_no_sanitize(undefined)]] auto append(message& m, ca::u32 value) noexcept -> void {

		char digits[10];
		ca::usz count = 0U;
		do {
			digits[count++] = static_cast<char>('0' + value % 10U);
			value /= 10U;
		} while (value != 0U);
		while (count != 0U && m._size < sizeof(m._bytes))
			m._bytes[m._size++] = static_cast<ca::byte>(digits[--count]);
	}

	/* The trap of check, failed at where. */
	[[noreturn, gnu::cold, gnu::noinline, ca_no_sanitize(undefined)]] auto undefined(const ubsan_location& where, const char* const check) noexcept -> void {

		ca::impl::trap_info info{};
		info._reason = check;
		 info._frame = ca::impl::frame_address();
		 info._where = ca::source_location{where._file, "", where._line};
		if (ca::impl::current_recovery != nullptr)
			static_cast<void>(ca::impl::raise(info));
		message m{{}, 0U};
		append(m, "ca: undefined behavior: ");
		append(m, check);
		append(m, " at ");
		append(m, where._file);
		append(m, ":");
		append(m, where._line);
		append(m, ":");
		append(m, where._column);
		append(m, "\n");
		static_cast<void>(ca::sys::write_all(2, ca::span<const ca::byte>{m._bytes, m._size}));
		__builtin_trap();
	}

	[[noreturn, gnu::cold, ca_no_sanitize(undefined)]] auto undefined(const void* const data, const char* const check) noexcept -> void {
		undefined(*static_cast<const ubsan_location*>(data), check);
	}

	[[noreturn, gnu::cold, ca_no_sanitize(undefined)]] auto type_mismatch(const void* const data, const ca::usz pointer) noexcept -> void {

		const ubsan_type_mismatch& d = *static_cast<const ubsan_type_mismatch*>(data);
		if (pointer == 0U)
			undefined(d._where, "null pointer use");
		if ((pointer & ((ca::usz{1U} << d._log_alignment) - 1U)) != 0U)
			undefined(d._where, "misaligned pointer use");
		undefined(d._where, "access past the end of an object");
	}

} // namespace


extern "C" { // ns-leak: ignore

	[[noreturn, gnu::weak]] auto __ubsan_handle_add_overflow(const void* d, ca::usz, ca::usz) noexcept -> void { undefined(d, "signed integer overflow"); } // ns-leak: ignore
	[[noreturn, gnu::weak]] auto __ubsan_handle_add_overflow_abort(const void* d, ca::usz, ca::usz) noexcept -> void { undefined(d, "signed integer overflow"); } // ns-leak: ignore
	[[noreturn, gnu::weak]] auto __ubsan_handle_sub_overflow(const void* d, ca::usz, ca::usz) noexcept -> void { undefined(d, "signed integer overflow"); } // ns-leak: ignore
	[[noreturn, gnu::weak]] auto __ubsan_handle_sub_overflow_abort(const void* d, ca::usz, ca::usz) noexcept -> void { undefined(d, "signed integer overflow"); } // ns-leak: ignore
	[[noreturn, gnu::weak]] auto __ubsan_handle_mul_overflow(const void* d, ca::usz, ca::usz) noexcept -> void { undefined(d, "signed integer overflow"); } // ns-leak: ignore
	[[noreturn, gnu::weak]] auto __ubsan_handle_mul_overflow_abort(const void* d, ca::usz, ca::usz) noexcept -> void { undefined(d, "signed integer overflow"); } // ns-leak: ignore
	[[noreturn, gnu::weak]] auto __ubsan_handle_negate_overflow(const void* d, ca::usz) noexcept -> void { undefined(d, "signed integer overflow"); } // ns-leak: ignore
	[[noreturn, gnu::weak]] auto __ubsan_handle_negate_overflow_abort(const void* d, ca::usz) noexcept -> void { undefined(d, "signed integer overflow"); } // ns-leak: ignore
	[[noreturn, gnu::weak]] auto __ubsan_handle_divrem_overflow(const void* d, ca::usz, ca::usz) noexcept -> void { undefined(d, "division overflow or by zero"); } // ns-leak: ignore
	[[noreturn, gnu::weak]] auto __ubsan_handle_divrem_overflow_abort(const void* d, ca::usz, ca::usz) noexcept -> void { undefined(d, "division overflow or by zero"); } // ns-leak: ignore
	[[noreturn, gnu::weak]] auto __ubsan_handle_shift_out_of_bounds(const void* d, ca::usz, ca::usz) noexcept -> void { undefined(d, "shift out of bounds"); } // ns-leak: ignore
	[[noreturn, gnu::weak]] auto __ubsan_handle_shift_out_of_bounds_abort(const void* d, ca::usz, ca::usz) noexcept -> void { undefined(d, "shift out of bounds"); } // ns-leak: ignore
	[[noreturn, gnu::weak]] auto __ubsan_handle_out_of_bounds(const void* d, ca::usz) noexcept -> void { undefined(d, "index out of bounds"); } // ns-leak: ignore
	[[noreturn, gnu::weak]] auto __ubsan_handle_out_of_bounds_abort(const void* d, ca::usz) noexcept -> void { undefined(d, "index out of bounds"); } // ns-leak: ignore
	[[noreturn, gnu::weak]] auto __ubsan_handle_type_mismatch_v1(const void* d, ca::usz pointer) noexcept -> void { type_mismatch(d, pointer); } // ns-leak: ignore
	[[noreturn, gnu::weak]] auto __ubsan_handle_type_mismatch_v1_abort(const void* d, ca::usz pointer) noexcept -> void { type_mismatch(d, pointer); } // ns-leak: ignore
	[[noreturn, gnu::weak]] auto __ubsan_handle_alignment_assumption(const void* d, ca::usz, ca::usz, ca::usz) noexcept -> void { undefined(d, "alignment assumption violated"); } // ns-leak: ignore
	[[noreturn, gnu::weak]] auto __ubsan_handle_alignment_assumption_abort(const void* d, ca::usz, ca::usz, ca::usz) noexcept -> void { undefined(d, "alignment assumption violated"); } // ns-leak: ignore
	[[noreturn, gnu::weak]] auto __ubsan_handle_builtin_unreachable(const void* d) noexcept -> void { undefined(d, "unreachable code reached"); } // ns-leak: ignore
	[[noreturn, gnu::weak]] auto __ubsan_handle_missing_return(const void* d) noexcept -> void { undefined(d, "end of a value-returning function reached"); } // ns-leak: ignore
	[[noreturn, gnu::weak]] auto __ubsan_handle_vla_bound_not_positive(const void* d, ca::usz) noexcept -> void { undefined(d, "variable-length array bound not positive"); } // ns-leak: ignore
	[[noreturn, gnu::weak]] auto __ubsan_handle_vla_bound_not_positive_abort(const void* d, ca::usz) noexcept -> void { undefined(d, "variable-length array bound not positive"); } // ns-leak: ignore
	[[noreturn, gnu::weak]] auto __ubsan_handle_float_cast_overflow(const void* d, ca::usz) noexcept -> void { undefined(d, "float conversion out of range"); } // ns-leak: ignore
	[[noreturn, gnu::weak]] auto __ubsan_handle_float_cast_overflow_abort(const void* d, ca::usz) noexcept -> void { undefined(d, "float conversion out of range"); } // ns-leak: ignore
	[[noreturn, gnu::weak]] auto __ubsan_handle_load_invalid_value(const void* d, ca::usz) noexcept -> void { undefined(d, "invalid value loaded (bool or enum)"); } // ns-leak: ignore
	[[noreturn, gnu::weak]] auto __ubsan_handle_load_invalid_value_abort(const void* d, ca::usz) noexcept -> void { undefined(d, "invalid value loaded (bool or enum)"); } // ns-leak: ignore
	[[noreturn, gnu::weak]] auto __ubsan_handle_invalid_builtin(const void* d) noexcept -> void { undefined(d, "invalid argument to a builtin"); } // ns-leak: ignore
	[[noreturn, gnu::weak]] auto __ubsan_handle_invalid_builtin_abort(const void* d) noexcept -> void { undefined(d, "invalid argument to a builtin"); } // ns-leak: ignore
	[[noreturn, gnu::weak]] auto __ubsan_handle_function_type_mismatch(const void* d, ca::usz) noexcept -> void { undefined(d, "call through a function pointer of another type"); } // ns-leak: ignore
	[[noreturn, gnu::weak]] auto __ubsan_handle_function_type_mismatch_abort(const void* d, ca::usz) noexcept -> void { undefined(d, "call through a function pointer of another type"); } // ns-leak: ignore
	[[noreturn, gnu::weak]] auto __ubsan_handle_implicit_conversion(const void* d, ca::usz, ca::usz) noexcept -> void { undefined(d, "implicit conversion changed the value"); } // ns-leak: ignore
	[[noreturn, gnu::weak]] auto __ubsan_handle_implicit_conversion_abort(const void* d, ca::usz, ca::usz) noexcept -> void { undefined(d, "implicit conversion changed the value"); } // ns-leak: ignore
	[[noreturn, gnu::weak]] auto __ubsan_handle_nonnull_arg(const void* d) noexcept -> void { undefined(d, "null passed as a nonnull argument"); } // ns-leak: ignore
	[[noreturn, gnu::weak]] auto __ubsan_handle_nonnull_arg_abort(const void* d) noexcept -> void { undefined(d, "null passed as a nonnull argument"); } // ns-leak: ignore
	[[noreturn, gnu::weak]] auto __ubsan_handle_nullability_arg(const void* d) noexcept -> void { undefined(d, "null passed as a nonnull argument"); } // ns-leak: ignore
	[[noreturn, gnu::weak]] auto __ubsan_handle_nullability_arg_abort(const void* d) noexcept -> void { undefined(d, "null passed as a nonnull argument"); } // ns-leak: ignore
	[[noreturn, gnu::weak]] auto __ubsan_handle_nonnull_return_v1(const void*, const void* where) noexcept -> void { undefined(where, "null returned from a nonnull function"); } // ns-leak: ignore
	[[noreturn, gnu::weak]] auto __ubsan_handle_nonnull_return_v1_abort(const void*, const void* where) noexcept -> void { undefined(where, "null returned from a nonnull function"); } // ns-leak: ignore
	[[noreturn, gnu::weak]] auto __ubsan_handle_nullability_return_v1(const void*, const void* where) noexcept -> void { undefined(where, "null returned from a nonnull function"); } // ns-leak: ignore
	[[noreturn, gnu::weak]] auto __ubsan_handle_nullability_return_v1_abort(const void*, const void* where) noexcept -> void { undefined(where, "null returned from a nonnull function"); } // ns-leak: ignore
	[[noreturn, gnu::weak]] auto __ubsan_handle_pointer_overflow(const void* d, ca::usz, ca::usz) noexcept -> void { undefined(d, "pointer arithmetic overflow"); } // ns-leak: ignore
	[[noreturn, gnu::weak]] auto __ubsan_handle_pointer_overflow_abort(const void* d, ca::usz, ca::usz) noexcept -> void { undefined(d, "pointer arithmetic overflow"); } // ns-leak: ignore

} // extern "C"
