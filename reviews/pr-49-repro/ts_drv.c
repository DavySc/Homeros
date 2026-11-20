/* org_ts at every byte of each file, printed as tests/org_drv.c prints a timestamp */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "org.h"
static void num(FILE *o, int v) { if (v < 0) fprintf(o, " -"); else fprintf(o, " %d", v); }
static const char *unit(char u) { return u == 'h' ? "hour" : u == 'd' ? "day" : u == 'w' ? "week" : u == 'm' ? "month" : "year"; }
static void ts(FILE *o, const char *k, const OrgTs *t)
{
	fprintf(o, "@ %s %s %s", k,
	        t->type == 'a' ? "active" : t->type == 'A' ? "active-range" : t->type == 'i' ? "inactive" :
	        t->type == 'I' ? "inactive-range" : "diary",
	        t->range == 'd' ? "daterange" : t->range == 't' ? "timerange" : "-");
	num(o, t->y); num(o, t->m); num(o, t->d); num(o, t->h); num(o, t->min);
	num(o, t->y2); num(o, t->m2); num(o, t->d2); num(o, t->h2); num(o, t->min2);
	if (t->rep) fprintf(o, " %s %d %s", t->rep == '+' ? "cumulate" : t->rep == '#' ? "catch-up" : "restart", t->rep_n, unit(t->rep_unit));
	else fprintf(o, " - - -");
	if (t->rep_dl_unit) fprintf(o, " %d %s", t->rep_dl_n, unit(t->rep_dl_unit)); else fprintf(o, " - -");
	if (t->warn) fprintf(o, " %s %d %s", t->warn == 'a' ? "all" : "first", t->warn_n, unit(t->warn_unit));
	else fprintf(o, " - - -");
	fprintf(o, " %zu %zu\n", t->begin, t->end);
}
int main(int argc, char **argv)
{
	for (int i = 1; i < argc; i++) {
		FILE *f = fopen(argv[i], "rb");
		if (!f) return 1;
		static char b[1 << 20];
		size_t n = fread(b, 1, sizeof b, f);
		fclose(f);
		char *s = malloc(n + 1); memcpy(s, b, n);   /* exact size, so ASan sees reads past n */
		const char *nm = strrchr(argv[i], '/');
		printf("== %s\n", nm ? nm + 1 : argv[i]);
		for (size_t p = 0; p < n; p++) {
			OrgTs t;
			if ((s[p] & 0xc0) != 0x80 && org_ts(s, n, p, &t)) ts(stdout, "ts", &t);
		}
		free(s);
	}
	return 0;
}
