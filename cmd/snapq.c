/* snapq SNAPSHOT [dialect]: answers lookups read from stdin, one per line,
   for the oracle (oracle/snap_oracle.py):
     o <schema or -> <name>       -> index schema name type ncol (tab-separated), or -1/-2
     c <schema> <name> <column>   -> column index name type
     m <schema> <name>            -> bytes and FNV-1a 32 of the module text
     k                            -> every foreign key column pair, one per line, then a blank line
   Fields are tab-separated; names are given as written in SQL ([x], "x"
   or bare). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "snap.h"

static char *slurp(const char *path, size_t *n)
{
	FILE *f = fopen(path, "rb");
	char *b = NULL;
	if (!f) return NULL;
	fseek(f, 0, SEEK_END);
	long len = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (len >= 0 && (b = malloc((size_t)len + 1)) && fread(b, 1, (size_t)len, f) != (size_t)len) free(b), b = NULL;
	fclose(f);
	*n = (size_t)len;
	return b;
}

int main(int argc, char **argv)
{
	size_t n, bad;
	char *src = argc > 1 ? slurp(argv[1], &n) : NULL;
	if (!src) { fprintf(stderr, "usage: snapq SNAPSHOT [tsql|pg|db2]\n"); return 2; }
	int d = argc > 2 && !strcmp(argv[2], "pg") ? D_PG : argc > 2 && !strcmp(argv[2], "db2") ? D_DB2 : D_TSQL;
	Snap *s = snap_load(src, n, d, &bad);
	free(src);
	if (!s) return 1;
	printf("objects %zu columns %zu fks %zu bad %zu\n", s->no, s->nc, s->nk, bad);
	char line[4096], b[3][4096];
	while (fgets(line, sizeof line, stdin)) {
		char *f[4], *p = line, op = line[0];
		int k = 0;
		line[strcspn(line, "\n")] = 0;
		while (k < 4) { f[k++] = p; if (!(p = strchr(p, '\t'))) break; *p++ = 0; }
		char **a = f + 1;
		if (op == 'k') {
			for (size_t i = 0; i < s->nk; i++) {
				const SnapFk *f = &s->k[i];
				printf("%s.%s.%s\t%s.%s.%s\n", s->o[f->from].schema, s->o[f->from].name, s->c[f->from_col].name,
				       s->o[f->to].schema, s->o[f->to].name, s->c[f->to_col].name);
			}
			printf("\n");
			continue;
		}
		if (k < 3) { printf("?\n"); continue; }
		Name sc = strcmp(a[0], "-") ? snap_name(a[0], strlen(a[0]), b[0]) : (Name){0};
		Name nm = snap_name(a[1], strlen(a[1]), b[1]);
		long o = snap_object(s, sc, nm);
		if (op == 'o' && k == 3) {
			if (o < 0) printf("%ld\n", o);
			else printf("%ld\t%s\t%s\t%s\t%u\n", o, s->o[o].schema, s->o[o].name, s->o[o].type, s->o[o].ncol);
		} else if (op == 'c' && k == 4) {
			long c = o >= 0 ? snap_column(s, (size_t)o, snap_name(a[2], strlen(a[2]), b[2])) : -1;
			if (c < 0) printf("-1\n");
			else printf("%ld\t%s\t%s\n", c, s->c[c].name, s->c[c].type);
		} else if (op == 'm' && k == 3 && o >= 0 && s->o[o].text) {
			unsigned h = 2166136261u;
			const char *t = s->o[o].text;
			for (size_t i = 0; t[i]; i++) h = (h ^ (unsigned char)t[i]) * 16777619u;
			printf("%zu %08x\n", strlen(t), h);
		} else printf("?\n");
		fflush(stdout);
	}
	snap_free(s);
	return 0;
}
