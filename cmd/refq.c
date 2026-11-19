/* refq SNAPSHOT OP [POS] < SQL: a refactor on the whole text, for the
   oracle (oracle/refactor_oracle.py).  OP: star, qualify (prints the new
   text), info (prints what the name at byte POS is), same FILE (prints
   whether FILE means what the input means: the meaning check alone).  A refusal prints
   "REFUSED: <why>" and exits 3. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "refactor.h"

static char *slurp(FILE *f, size_t *n)
{
	size_t cap = 1 << 16;
	char *b = malloc(cap);
	*n = 0;
	for (size_t k; b && (k = fread(b + *n, 1, cap - *n, f)) > 0;)
		if ((*n += k) == cap) b = realloc(b, cap *= 2);
	return b;
}

int main(int argc, char **argv)
{
	size_t n, m, len;
	FILE *f = argc > 2 ? fopen(argv[1], "rb") : NULL;
	char *snap = f ? slurp(f, &n) : NULL, *sql = slurp(stdin, &m);
	if (!snap || !sql) { fprintf(stderr, "usage: refq SNAPSHOT star|qualify|info [POS] < SQL\n"); return 2; }
	Snap *s = snap_load(snap, n, D_TSQL, NULL);
	Res r;
	Edits ed = {0};
	if (!s || resolve(&r, s, sql, m) < 0) return 1;
	int rc = !strcmp(argv[2], "star") ? refactor_star(&r, 0, m, &ed)
	       : !strcmp(argv[2], "qualify") ? refactor_qualify(&r, 0, m, &ed)
	       : !strcmp(argv[2], "info") && argc > 3 ? refactor_info(&r, strtoul(argv[3], 0, 10), &ed) : -3;
	int out = 0;
	if (!strcmp(argv[2], "same") && argc > 3 && (f = fopen(argv[3], "rb"))) {
		char *t = slurp(f, &len);
		fclose(f);
		puts(t && refactor_same(s, sql, m, t, len) ? "same" : "differ");
		free(t);
	} else if (rc == -2) printf("REFUSED: %s\n", ed.why), out = 3;
	else if (rc) out = 1;
	else if (!strcmp(argv[2], "info")) fputs(ed.buf ? ed.buf : "", stdout);
	else {
		char *t = edits_apply(&ed, sql, m, &len);
		if (!t) out = 1;
		else if (!refactor_same(s, sql, m, t, len)) printf("REFUSED: the meaning would change\n"), out = 3;
		else fwrite(t, 1, len, stdout);
		free(t);
	}
	edits_free(&ed);
	resolve_free(&r);
	snap_free(s);
	free(snap), free(sql);
	return out;
}
