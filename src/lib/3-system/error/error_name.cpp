#include "3-system/error/errc.hpp"
#include "3-system/error/error_name.hpp"


[[nodiscard]] auto ca::sys::error_name(const ca::error error) noexcept -> ca::sv::str {
	switch (error.code()) {
		case ca::errc::not_permitted.code():    return "not_permitted";
		case ca::errc::no_entry.code():         return "no_entry";
		case ca::errc::no_process.code():       return "no_process";
		case ca::errc::interrupted.code():      return "interrupted";
		case ca::errc::io_error.code():         return "io_error";
		case ca::errc::bad_descriptor.code():   return "bad_descriptor";
		case ca::errc::no_child.code():         return "no_child";
		case ca::errc::out_of_memory.code():    return "out_of_memory";
		case ca::errc::bad_address.code():      return "bad_address";
		case ca::errc::access_denied.code():    return "access_denied";
		case ca::errc::busy.code():             return "busy";
		case ca::errc::exists.code():           return "exists";
		case ca::errc::cross_device.code():     return "cross_device";
		case ca::errc::not_a_directory.code():  return "not_a_directory";
		case ca::errc::is_a_directory.code():   return "is_a_directory";
		case ca::errc::invalid_argument.code(): return "invalid_argument";
		case ca::errc::too_many_files.code():   return "too_many_files";
		case ca::errc::not_a_terminal.code():   return "not_a_terminal";
		case ca::errc::no_space.code():         return "no_space";
		case ca::errc::not_seekable.code():     return "not_seekable";
		case ca::errc::broken_pipe.code():      return "broken_pipe";
		case ca::errc::out_of_domain.code():    return "out_of_domain";
		case ca::errc::out_of_range.code():     return "out_of_range";
		case ca::errc::try_again.code():        return "try_again";
		case ca::errc::name_too_long.code():    return "name_too_long";
		case ca::errc::not_empty.code():        return "not_empty";
		case ca::errc::symlink_loop.code():     return "symlink_loop";
		case ca::errc::not_supported.code():    return "not_supported";
		case ca::errc::address_family_not_supported.code(): return "address_family_not_supported";
		case ca::errc::connection_refused.code(): return "connection_refused";
		case ca::errc::not_implemented.code():  return "not_implemented";
		case ca::errc::timed_out.code():        return "timed_out";
		case ca::errc::not_recoverable.code():  return "not_recoverable";
		default:                                return {};
	}
}
