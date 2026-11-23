/* include/date.h: every day from 1 Jan -5000 to 31 Dec 9999 round-trips,
   one day apart, as the calendar counts (Hinnant's own test: civil_from_days
   then days_from_civil over a range of years); months past 12 roll over. */
#include <stdio.h>
#include "check.h"
#include "date.h"

int main(void)
{
	long z = date_days(-5000, 1, 1);
	int py = -5001, pm = 12, pd = 31;
	for (; ; z++) {
		int y, m, d;
		date_civil(z, &y, &m, &d);
		CHECK(date_days(y, m, d) == z);
		int leap = y % 4 == 0 && (y % 100 != 0 || y % 400 == 0);
		int len = m == 2 ? 28 + leap : m == 4 || m == 6 || m == 9 || m == 11 ? 30 : 31;
		CHECK(m >= 1 && m <= 12 && d >= 1 && d <= len);
		/* the day after the one before */
		CHECK(d == pd + 1 ? m == pm && y == py : d == 1 && (m == pm + 1 ? y == py : m == 1 && pm == 12 && y == py + 1));
		py = y, pm = m, pd = d;
		if (y == 9999 && m == 12 && d == 31) break;
	}
	CHECK(date_days(1970, 1, 1) == 0 && date_days(2000, 3, 1) == 11017);
	CHECK(date_days(2026, 26, 1) == date_days(2028, 2, 1) && date_days(2026, -11, 1) == date_days(2025, 1, 1));
	CHECK(date_fdiv(-1, 1440) == -1 && date_fdiv(1439, 1440) == 0 && date_fdiv(-1440, 1440) == -1);
	puts("test_date: every day from -5000 to 9999 round-trips one day apart; months past 12 roll over; floor division");
	return 0;
}
