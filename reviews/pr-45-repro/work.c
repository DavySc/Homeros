/* Does org_edit save work?  Built against a copy of org.c with three
   counters (org_stat.c: nodes produced by parse(), calls of parse(), whole
   reparses).  For each file: N one-byte inserts of 'x' at random places,
   each then deleted again (2N edits), as typing does; prints per file the
   nodes parsed per edit against the file's node count, the whole reparses,
   and the time per edit against one org_parse.
   work SEED N FILE... */
#define _POSIX_C_SOURCE 200809L
#include "common.h"
#include <time.h>
extern size_t org_stat_nodes, org_stat_parses, org_stat_whole;
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e6 + t.tv_nsec / 1e3; }
static int cmpd(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y); }
static unsigned long long rs;
static size_t rnd(size_t n) { rs = rs * 6364136223846793005ULL + 1442695040888963407ULL; return (size_t)((rs >> 33) % (n ? n : 1)); }
int main(int argc, char **argv)
{
	rs = strtoull(argv[1], NULL, 10);
	size_t n = (size_t)atol(argv[2]);
	const char *ch = getenv("INS") ? getenv("INS") : "x";
	size_t cl = strlen(ch);
	double *us = malloc(2 * n * sizeof *us);
	printf("%-28s %8s %7s %9s %7s %6s %9s %9s %9s %9s\n", "file", "bytes", "nodes", "nodes/ed", "share", "whole", "parse us", "med us", "p99 us", "max us");
	for (int i = 3; i < argc; i++) {
		FILE *fp = fopen(argv[i], "rb");
		if (!fp) return 2;
		fseek(fp, 0, SEEK_END); size_t len = (size_t)ftell(fp); rewind(fp);
		char *t = malloc(len + cl + 1);
		if (fread(t, 1, len, fp) != len) return 2;
		fclose(fp);
		size_t k, m;
		double pt[21];
		OrgNode *e = NULL;
		for (int r = 0; r < 21; r++) { free(e); double a = now(); e = org_parse(t, len, &k); pt[r] = now() - a; }
		qsort(pt, 21, sizeof *pt, cmpd);
		size_t k0 = k;
		org_stat_nodes = org_stat_parses = org_stat_whole = 0;
		for (size_t j = 0; j < n; j++) {
			size_t pos = getenv("POS") ? (size_t)atol(getenv("POS")) : rnd(len + 1);
			memmove(t + pos + cl, t + pos, len - pos); memcpy(t + pos, ch, cl);
			double a = now();
			if (org_edit(&e, &k, t, len + cl, pos, 0, cl)) return 3;
			us[2 * j] = now() - a;
			memmove(t + pos, t + pos + cl, len - pos);
			a = now();
			if (org_edit(&e, &k, t, len, pos, cl, 0)) return 3;
			us[2 * j + 1] = now() - a;
		}
		size_t sn = org_stat_nodes, sw = org_stat_whole;
		OrgNode *f = org_parse(t, len, &m);
		if (differ(e, k, f, m) != ORG_NONE) { printf("%s: differs after the edits\n", argv[i]); return 1; }
		qsort(us, 2 * n, sizeof *us, cmpd);
		const char *b = strrchr(argv[i], '/');
		printf("%-28.28s %8zu %7zu %9.1f %6.2f%% %6zu %9.1f %9.2f %9.1f %9.1f\n", b ? b + 1 : argv[i], len, k0, (double)sn / (2 * n),
		       k0 ? 100.0 * sn / (2 * n) / k0 : 0, sw, pt[10], us[n], us[2 * n * 99 / 100], us[2 * n - 1]);
		free(e); free(f); free(t);
	}
	return 0;
}
