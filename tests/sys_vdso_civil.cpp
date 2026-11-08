// expect: exit 0
// link: both
#include "3-system/syscall/auxiliary.hpp"
#include "3-system/syscall/vdso.hpp"
#include "3-system/time/clock.hpp"
#include "3-system/time/now.hpp"
#include "3-system/time/utc_seconds.hpp"
#include "3-system/time/utc_time.hpp"

static auto check(const ca::i64 seconds, const int year, const int month, const int day, const int hour, const int minute, const int second, const int weekday, const int year_day) -> bool {
	auto time = ca::sys::utc_time(seconds);
	if (!time)
		return false;
	const bool fields = time->year() == year && time->month() == month && time->day() == day && time->hour() == hour
		&& time->minute() == minute && time->second() == second && time->weekday() == weekday && time->year_day() == year_day;
	auto back = ca::sys::utc_seconds(*time);
	return fields && back && *back == seconds;
}

auto main(void) -> int {
	if (ca::syscall::auxiliary(6U).value_or(0U) != 4096U)
		return 1;
	if (ca::syscall::vdso_symbol("__vdso_clock_gettime") == nullptr || ca::syscall::vdso_symbol("__vdso_no_such_function") != nullptr)
		return 2;
	const ca::i64 before = ca::sys::now(ca::sys::clock::monotonic);
	const ca::i64 after = ca::sys::now(ca::sys::clock::monotonic);
	if (before <= 0 || after < before || ca::sys::now(ca::sys::clock::realtime) < 1700000000LL * 1000000000LL)
		return 3;
	if (!check(0, 1970, 1, 1, 0, 0, 0, 4, 1) || !check(-1, 1969, 12, 31, 23, 59, 59, 3, 365))
		return 4;
	if (!check(951782400, 2000, 2, 29, 0, 0, 0, 2, 60) || !check(1709210096, 2024, 2, 29, 12, 34, 56, 4, 60))
		return 5;
	if (!check(-62135596800LL, 1, 1, 1, 0, 0, 0, 1, 1) || !check(-2203891200LL, 1900, 3, 1, 0, 0, 0, 4, 60))
		return 6;
	if (!check(253402300799LL, 9999, 12, 31, 23, 59, 59, 5, 365))
		return 7;
	for (ca::i64 s = -5000000000LL; s < 5000000000LL; s += 7777777) {
		auto time = ca::sys::utc_time(s);
		if (!time)
			return 8;
		auto back = ca::sys::utc_seconds(*time);
		if (!back || *back != s)
			return 9;
	}
	const ca::sys::calendar_time carry{2023, 13, 1, 0, 0, 0, 0, 0, 0, false};
	auto carried = ca::sys::utc_seconds(carry);
	if (!carried || *carried != 1704067200)
		return 10;
	auto huge = ca::sys::utc_time(ca::i64{1} << 62);
	return huge ? 11 : 0;
}
