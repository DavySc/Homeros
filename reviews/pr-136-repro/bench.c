/* Review of PR 136: what one edit costs a Zig buffer in the editor, that is
   syntax_edit and the syntax_update after it (parse, colours, folds, levels),
   and zig fmt of the whole file.  usage: bench EDITS file...   (times in ms) */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "syntax.h"
#include "text.h"
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec * 1e-6; }
static int cmp(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y); }
static unsigned rs = 1;
static unsigned rnd(unsigned n) { rs = rs * 1103515245u + 12345u; return (rs >> 8) % n; }
int main(int c, char **v)
{
	int edits = atoi(v[1]);
	double *d = malloc(sizeof *d * (size_t)(edits * c)), *o = malloc(sizeof *o * (size_t)c), *fm = malloc(sizeof *fm * (size_t)c);
	size_t nd = 0, no = 0, r1, r2, big = 0; double bigmed = 0, bigmax = 0, bigopen = 0, bigfmt = 0;
	for (int i = 2; i < c; i++) {
		FILE *f = fopen(v[i], "rb"); if (!f) continue;
		fseek(f, 0, 2); long l = ftell(f); rewind(f);
		char *b = malloc((size_t)l + 1); if (fread(b, 1, (size_t)l, f) != (size_t)l) return 1; fclose(f);
		Text *t = text_new(b, (size_t)l);
		double t0 = now();
		Syntax *s = syntax_open(v[i], t);
		if (!s) return 2;
		if (i == 2) { syntax_update(s, t, NULL, &r1, &r2); syntax_free(s); t0 = now(); s = syntax_open(v[i], t); }   /* zig version's spawn: once per run */
		syntax_update(s, t, NULL, &r1, &r2);
		o[no] = now() - t0;
		const char *res; size_t rn;
		t0 = now(); syntax_zig_fmt(s, &res, &rn); fm[no] = now() - t0;
		size_t d0 = nd;
		for (int e = 0; e < edits; e++) {
			size_t at = rnd((unsigned)text_len(t) + 1);
			const char *ins = rnd(2) ? "x" : " ";
			t0 = now();
			syntax_edit(s, t, at, 0, ins, 1);
			text_insert(t, at, ins, 1);
			syntax_update(s, t, NULL, &r1, &r2);
			d[nd++] = now() - t0;
		}
		if ((size_t)l > big && edits) { big = (size_t)l; qsort(d + d0, nd - d0, sizeof *d, cmp); bigmed = d[d0 + (nd - d0) / 2]; bigmax = d[nd - 1]; bigopen = o[no]; bigfmt = fm[no]; }
		no++;
		syntax_free(s); text_free(t); free(b);
	}
	qsort(d, nd, sizeof *d, cmp); qsort(o, no, sizeof *o, cmp); qsort(fm, no, sizeof *fm, cmp);
	printf("%zu files, %zu edits: an edit median %.3f p99 %.3f max %.3f | open median %.3f max %.3f | zig fmt median %.3f max %.3f | largest file (%zu bytes): an edit median %.3f max %.3f, open %.3f, zig fmt %.3f\n",
	       no, nd, d[nd / 2], d[nd * 99 / 100], d[nd - 1], o[no / 2], o[no - 1], fm[no / 2], fm[no - 1], big, bigmed, bigmax, bigopen, bigfmt);
	return 0;
}
