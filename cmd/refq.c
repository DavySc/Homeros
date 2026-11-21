/* refq SNAPSHOT OP [POS] < SQL: a refactor on the whole text, for the
   oracle (oracle/refactor_oracle.py).  OP: star, qualify (prints the new
   text), info (prints what the name at byte POS is), same FILE (prints
   whether FILE means what the input means: the meaning check alone),
   rename SCHEMA.OBJECT.COLUMN NEW (the code after the column's rename),
   alias POS NEW (the alias or CTE name at byte POS, in its statement),
   inline POS (the CTE named at POS), extract POS (the derived table),
   script SCHEMA.OBJECT.COLUMN NEW (the script renaming it in the
   database, with every module of the snapshot; the input is not read).  A refusal prints
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
	Snap *s = snap_load(snap, n, D_TSQL, NULL), *after = s;
	Res r;
	Edits ed = {0};
	if (!s || resolve(&r, s, sql, m) < 0) return 1;
	int rc = !strcmp(argv[2], "star") ? refactor_star(&r, 0, m, &ed)
	       : !strcmp(argv[2], "qualify") ? refactor_qualify(&r, 0, m, &ed)
	       : !strcmp(argv[2], "info") && argc > 3 ? refactor_info(&r, strtoul(argv[3], 0, 10), &ed)
	       : !strcmp(argv[2], "inline") && argc > 3 ? refactor_inline_cte(&r, strtoul(argv[3], 0, 10), &ed)
	       : !strcmp(argv[2], "extract") && argc > 3 ? refactor_extract_cte(&r, strtoul(argv[3], 0, 10), &ed)
	       : !strcmp(argv[2], "alias") && argc > 4 ? refactor_rename_at(&r, strtoul(argv[3], 0, 10), (Name){argv[4], strlen(argv[4]), 1}, &ed) : -3;
	if ((!strcmp(argv[2], "rename") || !strcmp(argv[2], "script")) && argc > 4) {   /* the snapshot as it will be: the meaning check compares across */
		char *o = argv[3], *a = strchr(o, '.'), *b = a ? strchr(a + 1, '.') : NULL;
		long k = b ? snap_object(s, (Name){o, (size_t)(a - o), 0}, (Name){a + 1, (size_t)(b - a - 1), 0}) : -1;
		long c = k >= 0 ? snap_column(s, (size_t)k, (Name){b + 1, strlen(b + 1), 0}) : -1;
		if (c < 0 || !(after = snap_load(snap, n, D_TSQL, NULL))) { fprintf(stderr, "refq: no column %s\n", o); return 2; }
		snap_rename(after, (size_t)c, argv[4]);
		Name to = {argv[4], strlen(argv[4]), 1};
		rc = !strcmp(argv[2], "script") ? refactor_rename_script(s, after, c, to, &ed) : refactor_rename_col(&r, c, to, &ed);
	}
	int out = 0;
	if (!strcmp(argv[2], "same") && argc > 3 && (f = fopen(argv[3], "rb"))) {
		char *t = slurp(f, &len);
		fclose(f);
		puts(t && refactor_same(s, sql, m, s, t, len) ? "same" : "differ");
		free(t);
	} else if (rc == -2) printf("REFUSED: %s\n", ed.why), out = 3;
	else if (rc) out = 1;
	else if (!strcmp(argv[2], "info") || !strcmp(argv[2], "script")) fputs(ed.buf ? ed.buf : "", stdout);
	else {
		char *t = edits_apply(&ed, sql, m, &len);
		if (!t) out = 1;
		else if (!refactor_same(s, sql, m, after, t, len)) printf("REFUSED: the meaning would change\n"), out = 3;
		else fwrite(t, 1, len, stdout);
		free(t);
	}
	edits_free(&ed);
	resolve_free(&r);
	if (after != s) snap_free(after);
	snap_free(s);
	free(snap), free(sql);
	return out;
}
