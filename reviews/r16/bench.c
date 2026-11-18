/* Reviewer's timing of both org builds, one source: -DBUILD_A or -DBUILD_B.
   bench load FILE          read the file, make the Text (callgrind baseline)
   bench full FILE [REPS]   + REPS full parses; prints the median in ms
   bench edits FILE N KIND  N one-byte inserts, each followed by bringing the
                            parse up to date; prints median, p99, max in us.
                            KIND: r random places, alternately "x" and "\n";
                                  t typing "x" at one random place per 50 keys
   What "up to date" means:
     A:  org_edit (its incremental reparse)
     B:  text_read of the whole text + org_parse (B has no incremental API)
     B with SUBTREE: only the cost a caller would pay to reparse the innermost
         headline subtree holding the edit (text_read of it + org_parse); the
         splice into the old list is not written in B, so this is a lower bound */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "text.h"
#include "org.h"

static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
static int cmp(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y); }

int main(int argc, char **argv)
{
	FILE *f = fopen(argv[2], "rb");
	fseek(f, 0, SEEK_END); size_t k = (size_t)ftell(f); rewind(f);
	char *b = malloc(2 * k + 4096);
	if (fread(b, 1, k, f) != k) return 1;
	fclose(f);
	Text *t = text_new(b, k);
	size_t nodes = 0;
	if (!strcmp(argv[1], "load")) return 0;
	if (!strcmp(argv[1], "full")) {
		int reps = argc > 3 ? atoi(argv[3]) : 1;
		double *d = malloc((size_t)reps * sizeof *d);
		for (int r = 0; r < reps; r++) {
			double t0 = now();
#ifdef BUILD_A
			Org *o = org_new(t); nodes = org_count(o); org_free(o);
#else
			text_read(t, 0, b, k);
			OrgNode *e = org_parse(b, k, &nodes); free(e);
#endif
			d[r] = now() - t0;
		}
		qsort(d, (size_t)reps, sizeof *d, cmp);
		printf("full %zu bytes %zu elements %.3f ms\n", k, nodes, d[reps / 2] * 1e3);
		return 0;
	}
	int n = atoi(argv[3]), typing = argv[4][0] == 't';
	double *e = malloc((size_t)n * sizeof *e);
	srand(1);
#ifdef BUILD_A
	Org *o = org_new(t);
#else
	OrgNode *v = org_parse(b, k, &nodes);
#endif
	size_t pos = 0;
	for (int i = 0; i < n; i++) {
		size_t len = text_len(t);
		if (!typing || i % 50 == 0) pos = (size_t)rand() % (len + 1); else pos++;
		const char *s = typing || i % 2 ? "x" : "\n";
		text_insert(t, pos, s, 1);
		double t0 = now();
#ifdef BUILD_A
		org_edit(o, t, pos, 0, 1);
		nodes = org_count(o);
#elif defined SUBTREE
		size_t a = 0, z = len;   /* innermost headline of the old parse holding pos */
		for (size_t j = 0; j < nodes && v[j].begin <= pos; j++)
			if (v[j].type == ORG_HEADLINE && pos < v[j].end) { a = v[j].begin; z = v[j].end; }
		size_t m;
		text_read(t, a, b, z + 1 - a);
		OrgNode *w = org_parse(b, z + 1 - a, &m);
		free(w);
		e[i] = now() - t0;
		/* keep the list current for the next round (not timed) */
		free(v); text_read(t, 0, b, len + 1); v = org_parse(b, len + 1, &nodes);
		continue;
#else
		free(v); text_read(t, 0, b, len + 1); v = org_parse(b, len + 1, &nodes);
#endif
		e[i] = now() - t0;
	}
	qsort(e, (size_t)n, sizeof *e, cmp);
	printf("edits %s %zu elements median %.1f us p99 %.1f us max %.1f us\n", argv[4], nodes, e[n / 2] * 1e6, e[n * 99 / 100] * 1e6, e[n - 1] * 1e6);
	return 0;
}
