/* Proleptic Gregorian dates as days since 1970-01-01 (Howard Hinnant's
   days_from_civil and civil_from_days): org's timestamps and SQL Server's
   date types, one copy (date.c).  Thread 0's, agreed. */
#ifndef DATE_H
#define DATE_H

long date_fdiv(long a, long b);                    /* a / b rounded down */
long date_days(long y, long m, long d);            /* days since 1970-01-01; months past 12 roll over (Emacs's encode-time) */
void date_civil(long z, int *y, int *m, int *d);   /* day z since 1970-01-01 as a date */
#endif
