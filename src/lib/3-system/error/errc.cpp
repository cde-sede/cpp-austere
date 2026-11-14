#include "3-system/error/errc.hpp"

#include <errno.h>


static_assert(ca::errc::not_permitted.code() == EPERM);
static_assert(ca::errc::no_entry.code() == ENOENT);
static_assert(ca::errc::no_process.code() == ESRCH);
static_assert(ca::errc::interrupted.code() == EINTR);
static_assert(ca::errc::io_error.code() == EIO);
static_assert(ca::errc::bad_descriptor.code() == EBADF);
static_assert(ca::errc::no_child.code() == ECHILD);
static_assert(ca::errc::out_of_memory.code() == ENOMEM);
static_assert(ca::errc::bad_address.code() == EFAULT);
static_assert(ca::errc::access_denied.code() == EACCES);
static_assert(ca::errc::busy.code() == EBUSY);
static_assert(ca::errc::exists.code() == EEXIST);
static_assert(ca::errc::cross_device.code() == EXDEV);
static_assert(ca::errc::not_a_directory.code() == ENOTDIR);
static_assert(ca::errc::is_a_directory.code() == EISDIR);
static_assert(ca::errc::invalid_argument.code() == EINVAL);
static_assert(ca::errc::too_many_files.code() == EMFILE);
static_assert(ca::errc::not_a_terminal.code() == ENOTTY);
static_assert(ca::errc::no_space.code() == ENOSPC);
static_assert(ca::errc::not_seekable.code() == ESPIPE);
static_assert(ca::errc::broken_pipe.code() == EPIPE);
static_assert(ca::errc::out_of_domain.code() == EDOM);
static_assert(ca::errc::out_of_range.code() == ERANGE);
static_assert(ca::errc::try_again.code() == EAGAIN);
static_assert(ca::errc::name_too_long.code() == ENAMETOOLONG);
static_assert(ca::errc::not_empty.code() == ENOTEMPTY);
static_assert(ca::errc::symlink_loop.code() == ELOOP);
static_assert(ca::errc::not_supported.code() == ENOTSUP);
static_assert(ca::errc::address_family_not_supported.code() == EAFNOSUPPORT);
static_assert(ca::errc::connection_refused.code() == ECONNREFUSED);
static_assert(ca::errc::address_in_use.code() == EADDRINUSE);
static_assert(ca::errc::address_not_available.code() == EADDRNOTAVAIL);
static_assert(ca::errc::connection_aborted.code() == ECONNABORTED);
static_assert(ca::errc::connection_reset.code() == ECONNRESET);
static_assert(ca::errc::already_connected.code() == EISCONN);
static_assert(ca::errc::not_connected.code() == ENOTCONN);
static_assert(ca::errc::already_in_progress.code() == EALREADY);
static_assert(ca::errc::in_progress.code() == EINPROGRESS);
static_assert(ca::errc::not_implemented.code() == ENOSYS);
static_assert(ca::errc::timed_out.code() == ETIMEDOUT);
static_assert(ca::errc::not_recoverable.code() == ENOTRECOVERABLE);
