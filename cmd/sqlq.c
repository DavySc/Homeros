/* Bake-off driver for client/tds.c (built as tdsq) and client/pg.c (built
   with -DPG as pgq): tdsq [-q] [-t] [-c ROWS] host port user pass db < batch
   Runs the batch through one poll loop and prints the reply (rows and
   messages, in order) on stdout, then timings on stderr:
   login, first row, total (ms), rows, output bytes.  -q: count, print
   nothing.  -t: trust the server's certificate (a test server's own).  -c ROWS: after ROWS rows stop reading for 300 ms (a paused
   result), send an attention, and report how long the cancel took and
   how many rows still had to be read. */
#define _POSIX_C_SOURCE 200809L
#ifdef PG
#include "pg.h"
#define NAME "pgq"
#define Tds Pg
#define tds_open pg_open
#define tds_batch pg_query
#define tds_cancel pg_cancel
#define tds_pending pg_pending
#define tds_io pg_io
#define tds_close pg_close
#define TDS_READY PG_READY
#define TDS_BUSY PG_BUSY
#define TDS_CANCEL PG_CANCEL
#define TDS_DEAD PG_DEAD
#else
#include "tds.h"
#define NAME "tdsq"
#endif
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double now(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

static int run(Tds *t, int until, uint64_t stop)   /* loop until state is `until`, or stop rows */
{
	while (t->state != until && t->state != TDS_DEAD && !(stop && t->rows >= stop)) {
		struct pollfd p = {t->net.fd, (short)(POLLIN | (tds_pending(t) ? POLLOUT : 0)), 0};
		if (poll(&p, 1, -1) < 0 || tds_io(t, p.revents & (POLLIN | POLLHUP | POLLERR)) < 0) return -1;
	}
	return t->state == TDS_DEAD ? -1 : 0;
}

int main(int argc, char **argv)
{
	int quiet = 0, trust = 0, a = 1;
	uint64_t cancel = 0;
	for (; a < argc && argv[a][0] == '-'; a++)
		if (!strcmp(argv[a], "-q")) quiet = 1;
		else if (!strcmp(argv[a], "-t")) trust = 1;
		else if (!strcmp(argv[a], "-c") && a + 1 < argc) cancel = strtoull(argv[++a], NULL, 10);
	if (argc - a != 5) { fprintf(stderr, "usage: " NAME " [-q] [-t] [-c ROWS] host port user pass db < batch\n"); return 2; }
	static char sql[1 << 20];
	sql[fread(sql, 1, sizeof sql - 1, stdin)] = 0;
	Tds t;
	double t0 = now();
	if (tds_open(&t, argv[a], atoi(argv[a + 1]), argv[a + 2], argv[a + 3], argv[a + 4], trust) || run(&t, TDS_READY, 0)) {
		fprintf(stderr, NAME ": %s\n", t.net.err);
		tds_close(&t);
		return 1;
	}
	double t1 = now(), first = 0;
	t.out.n = 0;
	if (tds_batch(&t, sql)) return tds_close(&t), 1;
	while (t.state == TDS_BUSY || t.state == TDS_CANCEL) {
		if (run(&t, TDS_READY, t.rows + 1)) { fprintf(stderr, NAME ": %s\n", t.net.err); tds_close(&t); return 1; }
		if (t.rows && !first) first = now();
		if (!quiet) fwrite(t.out.p, 1, t.out.n, stdout);
		t.out.n = 0;
		if (cancel && t.rows >= cancel && t.state == TDS_BUSY) {
			struct timespec ms300 = {0, 300000000};
			nanosleep(&ms300, NULL);
			uint64_t at = t.rows;
			double c0 = now();
			if (tds_cancel(&t) || run(&t, TDS_READY, 0)) { fprintf(stderr, NAME ": %s\n", t.net.err); tds_close(&t); return 1; }
			fprintf(stderr, "cancel %.2f rows_after_cancel %llu\n", now() - c0, (unsigned long long)(t.rows - at));
			if (!quiet) fwrite(t.out.p, 1, t.out.n, stdout);
			t.out.n = 0;
		}
	}
	double t2 = now();
	fprintf(stderr, "login %.2f first_row %.2f total %.2f rows %llu\n", t1 - t0, first ? first - t1 : 0, t2 - t1, (unsigned long long)t.rows);
	tds_close(&t);
	return 0;
}
