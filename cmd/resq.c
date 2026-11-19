/* resq SNAPSHOT [FILE]: resolves FILE (default stdin) and prints, for the
   oracles (oracle/res_oracle.py):
     Q <block>                                 each statement's query
     B <block> <parent>
     O <block> <name or -> <origin>            one per output column
     R <kind> <status> <start> <end> <target> <text>  one per reference
   fields tab-separated
   origin/target: schema.object.column for a catalog column (followed
   through derived tables and CTEs), - when none. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "resolve.h"

static char *slurp(FILE *f, size_t *n)
{
	size_t cap = 1 << 16;
	char *b = malloc(cap);
	*n = 0;
	for (size_t k; b && (k = fread(b + *n, 1, cap - *n, f)) > 0;)
		if ((*n += k) == cap) b = realloc(b, cap *= 2);
	return b;
}

/* the catalog column behind column col of source sc, through derived
   tables and CTEs */
static void origin(const Res *r, int sc, int col)
{
	for (int guard = 0; sc >= 0 && col >= 0 && guard < 64; guard++) {
		const Src *s = &r->src[sc];
		if (s->kind == SRC_TABLE) {
			const SnapCol *c = &r->snap->c[r->snap->o[s->obj].col + col];
			printf("%s.%s.%s", r->snap->o[s->obj].schema, r->snap->o[s->obj].name, c->name);
			return;
		}
		if (s->inner < 0 || (uint32_t)col >= r->b[s->inner].nout) break;   /* a column list renames inner's outputs */
		const Out *o = &r->out[r->b[s->inner].out + col];
		sc = o->src, col = o->col;
	}
	printf("-");
}

int main(int argc, char **argv)
{
	size_t n, m;
	FILE *f = argc > 1 ? fopen(argv[1], "rb") : NULL, *q = argc > 2 ? fopen(argv[2], "rb") : stdin;
	char *snap = f ? slurp(f, &n) : NULL, *sql = q ? slurp(q, &m) : NULL;
	if (!snap || !sql) { fprintf(stderr, "usage: resq SNAPSHOT [FILE]\n"); return 2; }
	Snap *s = snap_load(snap, n, D_TSQL, NULL);
	Res r;
	if (!s || resolve(&r, s, sql, m) < 0) return 1;
	for (size_t t = 0; t < r.ntop; t++) printf("Q\t%d\n", r.top[t]);
	for (size_t b = 0; b < r.nb; b++) {
		printf("B\t%zu\t%d\n", b, r.b[b].parent);
		for (uint32_t i = 0; i < r.b[b].nout; i++) {
			const Out *o = &r.out[r.b[b].out + i];
			Name nm = out_name(&r, o);
			printf("O\t%zu\t%.*s\t", b, nm.s ? (int)nm.n : 1, nm.s ? nm.s : "-");
			origin(&r, o->src, o->col);
			printf("\n");
		}
	}
	static const char *const kinds[] = {"object", "alias", "cte-def", "cte", "qual", "column", "star", "out-def", "out"};
	for (size_t i = 0; i < r.nr; i++) {
		const Ref *x = &r.r[i];
		printf("R\t%s\t%d\t%u\t%u\t", kinds[x->kind], x->status, x->s, x->e);
		if (x->kind == RF_COLUMN && x->status == RS_OK) origin(&r, x->src, x->col);
		else printf("-");
		printf("\t%.*s\n", (int)(x->e - x->s), sql + x->s);
	}
	resolve_free(&r);
	snap_free(s);
	free(snap), free(sql);
	return 0;
}
