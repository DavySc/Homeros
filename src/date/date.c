/* include/date.h: Hinnant's algorithms (chrono-Compatible Low-Level Date
   Algorithms), moved here from org.c so the SQL client shares them. */
#include "date.h"

long date_fdiv(long a, long b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
long date_days(long y, long m, long d)
{
	y += date_fdiv(m - 1, 12), m -= 12 * date_fdiv(m - 1, 12);
	y -= m <= 2;
	long era = date_fdiv(y, 400), yoe = y - era * 400, doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
	return era * 146097 + yoe * 365 + yoe / 4 - yoe / 100 + doy - 719468;
}
void date_civil(long z, int *y, int *m, int *d)
{
	z += 719468;
	long era = date_fdiv(z, 146097), doe = z - era * 146097, yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	long doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
	*d = (int)(doy - (153 * mp + 2) / 5 + 1), *m = (int)(mp < 10 ? mp + 3 : mp - 9), *y = (int)(yoe + era * 400 + (*m <= 2));
}
