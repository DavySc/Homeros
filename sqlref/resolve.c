/* T-SQL name resolution (resolve.h; the rules are KB 03-resolver.md, each
   numbered there).  One pass over the significant tokens: queries are
   found wherever they start (SELECT, WITH, a parenthesised SELECT), and
   each block resolves its FROM first, so a name sees exactly the sources
   SQL Server lets it see at that point (rule 4: ON sees the joins so far,
   APPLY everything to its left), then the select list, WHERE, GROUP BY,
   HAVING, and ORDER BY last (rule 7).  A name nothing explains is marked
   with the error SQL Server would give, so a refactor can refuse. */
#include <stdlib.h>
#include <string.h>
#include "resolve.h"
#include "text.h"
#include "tsql.h"

typedef struct {
	uint32_t s, e;
	uint8_t k;          /* TQ_* */
	uint8_t rsv;        /* a reserved word (the formatter recases it) */
	int32_t m;          /* ( and ): the matching one, else -1 */
	int32_t d;          /* depth before it: parentheses and CASE..END */
	Name nm;            /* WORD, IDENT: the name it spells */
} T;

/* a CTE in scope: its definition, a source with blk -1 */
typedef struct { int32_t src; } Cte;

typedef struct {
	Res *res;
	T *t; size_t nt;
	char *low;          /* the text in ASCII lower case, for keyword tests */
	char *unq;          /* unquoted names */
	Cte *cte; size_t ncte, ctecap;
	int on_blk, on_grp;              /* an ON or APPLY being read: in on_blk
	                                    only comma group on_grp is seen */
	int grp;                         /* the FROM item being read */
	Cte *loc; size_t nloc, loccap;   /* temp tables and table variables made so far */
	int trig;                        /* a trigger's table, a source no block sees:
	                                    inserted, deleted, UPDATE(c) */
	int target;                      /* reading a DML target: no alias, no column list */
	size_t tcap;
	Out *pend; size_t npend, pcap;   /* select lists being read: a block's
	                                    outputs go to Res.out together when
	                                    it ends, after its subqueries' */
	size_t rcap, scap, bcap, ocap, ncap;
	int oom;
} P;

#define GROW(a, n, cap) ((n) < (cap) || grow((void **)&(a), &(cap), sizeof *(a), p))
static int grow(void **a, size_t *cap, size_t size, P *p)
{
	size_t c = *cap ? *cap * 2 : 64;
	void *q = realloc(*a, c * size);
	if (!q) return p->oom = 1, 0;
	*a = q, *cap = c;
	return 1;
}

/* ---- tokens ---- */

static int is(const P *p, size_t i, const char *w)
{
	size_t n = strlen(w);
	return i < p->nt && p->t[i].k == TQ_WORD && p->t[i].e - p->t[i].s == n && !memcmp(p->low + p->t[i].s, w, n);
}
static int punct(const P *p, size_t i, char c) { return i < p->nt && (p->t[i].k == TQ_PUNCT || p->t[i].k == TQ_OP) && p->t[i].e - p->t[i].s == 1 && p->res->text[p->t[i].s] == c; }
static int any(const P *p, size_t i, const char *const *w) { for (; *w; w++) if (is(p, i, *w)) return 1; return 0; }
/* a name: a word that is not reserved, or [x] / "x" */
static int name_tok(const P *p, size_t i) { return i < p->nt && (p->t[i].k == TQ_IDENT || (p->t[i].k == TQ_WORD && !p->t[i].rsv)); }
static int word_or_ident(const P *p, size_t i) { return i < p->nt && (p->t[i].k == TQ_IDENT || p->t[i].k == TQ_WORD); }

static const char *const starters[] = {"select", "insert", "update", "delete", "merge", "declare", "set", "if", "while", "begin",
	"end", "return", "exec", "execute", "print", "raiserror", "throw", "create", "alter", "drop", "truncate", "goto", "break",
	"continue", "open", "close", "fetch", "deallocate", "use", "waitfor", "commit", "rollback", "save", "else", "go", "grant",
	"revoke", "deny", "dbcc", "kill", "bulk", "readtext", "writetext", "updatetext", "checkpoint", "reconfigure", NULL};
static const char *const ends_block[] = {"union", "except", "intersect", "order", "option", NULL};
static const char *const for_clause[] = {"xml", "json", "browse", "update", "read", NULL};
/* a query's clauses end at i */
static int ends_query(const P *p, size_t i)
{
	return (any(p, i, ends_block) && (!is(p, i, "order") || is(p, i + 1, "by"))) || (is(p, i, "for") && any(p, i + 1, for_clause));
}

/* a CTE list starts here: WITH name [(cols)] AS ( */
static int cte_start(const P *p, size_t i)
{
	if (!is(p, i, "with") || !word_or_ident(p, i + 1)) return 0;
	size_t j = i + 2;
	if (punct(p, j, '(') && p->t[j].m > 0) j = (size_t)p->t[j].m + 1;
	return is(p, j, "as") && punct(p, j + 1, '(');
}
/* a statement starts at i (so a query ends before it) */
static int starts_statement(const P *p, size_t i)
{
	if (i >= p->nt) return 1;
	if (punct(p, i, ';')) return 1;
	if (is(p, i, "fetch") && i && (is(p, i - 1, "rows") || is(p, i - 1, "row"))) return 0;   /* OFFSET .. FETCH */
	return any(p, i, starters) || cte_start(p, i);
}

/* the token starting at byte s (or the first after it) */
static size_t tok_at(const P *p, uint32_t s)
{
	size_t a = 0, z = p->nt;
	while (a < z) { size_t m = a + (z - a) / 2; if (p->t[m].s < s) a = m + 1; else z = m; }
	return a;
}

/* ---- the result ---- */

static Name nm_span(const P *p, uint32_t s) { return p->t[tok_at(p, s)].nm; }

Name out_name(const Res *res, const Out *o)
{
	if (o->cat) return (Name){o->cat, strlen(o->cat), 1};
	if (o->ne > o->ns) return nm_span(res->priv, o->ns);
	return (Name){0};
}

size_t src_ncols(const Res *res, size_t sc)
{
	const Src *s = &res->src[sc];
	if (s->kind == SRC_TABLE) return res->snap->o[s->obj].ncol;
	if (s->list) return s->ncols;
	if (s->kind == SRC_UNKNOWN || s->inner < 0) return SIZE_MAX;
	return res->b[s->inner].nout;
}
static const Out *src_out(const Res *res, size_t sc, size_t i)
{
	const Src *s = &res->src[sc];
	return s->list ? &res->names[s->cols + i] : &res->out[res->b[s->inner].out + i];
}
Name src_col(const Res *res, size_t sc, size_t i, long *cat)
{
	const Src *s = &res->src[sc];
	*cat = -1;
	if (s->kind == SRC_TABLE) {
		*cat = (long)(res->snap->o[s->obj].col + i);
		return (Name){res->snap->c[*cat].name, strlen(res->snap->c[*cat].name), 1};
	}
	return out_name(res, src_out(res, sc, i));
}

int res_tok(const Res *r, uint32_t pos, int dir, uint32_t *s, uint32_t *e)
{
	const P *p = r->priv;
	size_t i = tok_at(p, pos);
	if (dir < 0) {   /* the last one ending at or before pos */
		while (i > 0 && p->t[i - 1].e > pos) i--;
		if (!i) return 0;
		i--;
	} else if (i >= p->nt) return 0;
	*s = p->t[i].s, *e = p->t[i].e;
	return 1;
}
uint32_t res_starter(const Res *r, uint32_t pos)
{
	const P *p = r->priv;
	size_t i = tok_at(p, pos), k = i;
	int32_t d = i < p->nt ? p->t[i].d : 0;
	while (k-- > 0 && (p->t[k].d != d || !starts_statement(p, k)));
	return k < i ? p->t[k].s : UINT32_MAX;
}
uint32_t res_match(const Res *r, uint32_t pos)
{
	const P *p = r->priv;
	size_t i = tok_at(p, pos);
	return i < p->nt && p->t[i].s == pos && p->t[i].m >= 0 ? p->t[p->t[i].m].s : UINT32_MAX;
}

long res_origin(const Res *r, int sc, int col)
{
	for (int guard = 0; sc >= 0 && col >= 0 && guard < 256; guard++) {
		const Src *s = &r->src[sc];
		if (s->kind == SRC_TABLE) return (long)(r->snap->o[s->obj].col + (uint32_t)col);
		if (s->inner < 0 || (uint32_t)col >= r->b[s->inner].nout) return -1;   /* a column list renames inner's outputs */
		const Out *o = &r->out[r->b[s->inner].out + (uint32_t)col];
		if (o->src < 0) return -2 - ((long)s->inner << 16 | col);
		sc = o->src, col = o->col;
	}
	return -1;
}

static void ref(P *p, uint32_t s, uint32_t e, int kind, int status, int src, int col)
{
	Res *r = p->res;
	if (!GROW(r->r, r->nr, p->rcap)) return;
	r->r[r->nr++] = (Ref){s, e, (uint8_t)kind, (uint16_t)status, src, col, 0};
}
static int new_src(P *p, int kind, int blk)
{
	Res *r = p->res;
	if (!GROW(r->src, r->nsrc, p->scap)) return -1;
	r->src[r->nsrc] = (Src){(uint8_t)kind, blk, -1, -1, 0, 0, 0, 0, 0, 0, 0, 0, 0, (uint16_t)p->grp};
	return (int)r->nsrc++;
}
static int new_blk(P *p, int parent, size_t i)
{
	Res *r = p->res;
	if (!GROW(r->b, r->nb, p->bcap)) return -1;
	r->b[r->nb] = (Blk){parent, p->t[i].s, p->t[i].e, (uint32_t)r->nout, 0};
	return (int)r->nb++;
}
static void out(P *p, int blk, Out o)
{
	if (blk < 0 || !GROW(p->pend, p->npend, p->pcap)) return;
	p->pend[p->npend++] = o;
	p->res->b[blk].nout++;
}
/* block blk's outputs, pend[base ..), into Res.out */
static void commit(P *p, int blk, size_t base)
{
	Res *r = p->res;
	r->b[blk].out = (uint32_t)r->nout;
	for (size_t i = base; i < p->npend; i++)
		if (GROW(r->out, r->nout, p->ocap)) r->out[r->nout++] = p->pend[i];
	p->npend = base;
}

/* ---- looking names up ---- */

/* column c of source sc: its index, -1 none, -2 unknown columns */
static long col_in(const P *p, int sc, Name c)
{
	const Res *r = p->res;
	size_t n = src_ncols(r, (size_t)sc);
	if (n == SIZE_MAX) return -2;
	if (r->src[sc].kind == SRC_TABLE) {
		long k = snap_column(r->snap, (size_t)r->src[sc].obj, c);
		return k < 0 ? -1 : k - (long)r->snap->o[r->src[sc].obj].col;
	}
	long hit = -1;
	for (size_t i = 0; i < n; i++) {
		Name o = out_name(r, src_out(r, (size_t)sc, i));
		if (o.s && snap_eq(r->snap, o, c)) { if (hit >= 0) return -3; hit = (long)i; }   /* -3: two columns of that name */
	}
	return hit;
}

/* source s is one a name in block blk sees: an ON or an APPLY sees only
   its own comma-separated item of the FROM list (a, b JOIN c ON .. : not a) */
static int seen(const P *p, const Src *s, int blk)
{
	return s->blk == blk && !s->hidden && (blk != p->on_blk || s->grp == p->on_grp);
}

/* the source a qualifier names (parts q[0..nq)), looking from blk outward;
   -1 none */
static int find_src(const P *p, int blk, const Name *q, int nq)
{
	const Res *r = p->res;
	for (; blk >= 0; blk = r->b[blk].parent)
		for (size_t i = r->nsrc; i-- > 0;) {
			const Src *s = &r->src[i];
			if (!seen(p, s, blk)) continue;
			if (nq == 1) {
				if (s->ne > s->ns && snap_eq(r->snap, nm_span(p, s->ns), q[0])) return (int)i;
				continue;
			}
			/* schema.table matches a table without an alias (rule 1) */
			if (s->kind != SRC_TABLE || !s->ne || !snap_eq(r->snap, nm_span(p, s->ns), q[nq - 1])) continue;
			const SnapObj *o = &r->snap->o[s->obj];
			if (snap_eq(r->snap, (Name){o->schema, strlen(o->schema), 1}, q[nq - 2]) && snap_eq(r->snap, (Name){o->name, strlen(o->name), 1}, nm_span(p, s->ns)))
				return (int)i;
		}
	return -1;
}

/* an unqualified column, from blk outward (rule 5): sets *src, *col;
   returns the status */
static int find_col(const P *p, int blk, Name c, int *src, long *col)
{
	const Res *r = p->res;
	*src = -1, *col = -1;
	for (; blk >= 0; blk = r->b[blk].parent) {
		int hits = 0, unsure = 0;
		for (size_t i = 0; i < r->nsrc; i++) {
			const Src *s = &r->src[i];
			if (!seen(p, s, blk)) continue;
			long k = col_in(p, (int)i, c);
			if (k == -2) unsure = 1;
			else if (k == -3) hits += 2;
			else if (k >= 0) hits++, *src = (int)i, *col = k;
		}
		if (hits > 1) return RS_AMBIG;
		if (hits == 1) return unsure ? RS_UNSURE : RS_OK;
		if (unsure) return *src = -1, RS_UNSURE;
	}
	return RS_NOCOL;
}

/* ---- expressions ---- */

static size_t query(P *p, size_t i, int parent, int *first, int cte);
static void expr(P *p, size_t a, size_t z, int blk);
static int dml_at(const P *p, size_t i);
static size_t dml(P *p, size_t i, int parent);

/* the end of a dotted name starting at i: parts into q (at most 4) */
static size_t chain(const P *p, size_t i, Name *q, int *nq, size_t *last)
{
	*nq = 0;
	for (;;) {
		if (*nq < 4) q[*nq] = p->t[i].nm;
		++*nq, *last = i;
		if (!(punct(p, i + 1, '.') && (word_or_ident(p, i + 2) || punct(p, i + 2, '*')))) return i + 1;
		if (punct(p, i + 2, '*')) return i + 1;   /* x.* : the caller's */
		i += 2;
	}
}

/* words that are keywords inside expressions without being reserved
   (OVER (PARTITION BY .. ROWS BETWEEN UNBOUNDED PRECEDING ..), AT TIME
   ZONE, WITHIN GROUP, IGNORE NULLS ..) */
static const char *const soft[] = {"partition", "rows", "range", "unbounded", "preceding", "following", "row", "first",
	"last", "next", "only", "ties", "zone", "time", "at", "within", "ignore", "respect", "nulls", "asc", "desc",
	"current_user", "session_user", "system_user", "user", "current_timestamp", NULL};

/* a column reference: parts q[0..nq), tokens a..last */
static void colref(P *p, size_t a, size_t last, const Name *q, int nq, int blk, int *osrc, long *ocol)
{
	int src = -1, st;
	long col = -1;
	if (nq > 4) { ref(p, p->t[a].s, p->t[last].e, RF_COLUMN, RS_UNSURE, -1, -1); return; }
	if (nq == 1) {
		st = find_col(p, blk, q[0], &src, &col);
		/* a non-reserved keyword of an expression, when nothing else names it */
		if (st == RS_NOCOL && any(p, a, soft)) return;
	} else {
		src = find_src(p, blk, q, nq - 1);
		size_t qe = last - 2;   /* the qualifier's last token */
		if (src < 0) {
			ref(p, p->t[a].s, p->t[qe].e, RF_QUAL, RS_UNBOUND, -1, -1);
			ref(p, p->t[last].s, p->t[last].e, RF_COLUMN, RS_UNBOUND, -1, -1);
			return;
		}
		ref(p, p->t[a].s, p->t[qe].e, RF_QUAL, RS_OK, src, -1);
		col = col_in(p, src, q[nq - 1]);
		st = col >= 0 ? RS_OK : col == -2 ? RS_UNSURE : col == -3 ? RS_AMBIG : RS_NOCOL;
		if (col < 0) col = -1;
	}
	ref(p, p->t[last].s, p->t[last].e, RF_COLUMN, st, src, (int)col);
	if (osrc && st == RS_OK) *osrc = src, *ocol = col;
}

/* the column named by token k, in source sc only */
static void col_of(P *p, size_t k, int sc)
{
	long c = col_in(p, sc, p->t[k].nm);
	ref(p, p->t[k].s, p->t[k].e, RF_COLUMN, c >= 0 ? RS_OK : c == -2 ? RS_UNSURE : RS_NOCOL, sc, c >= 0 ? (int)c : -1);
}

static const char *const type_first[] = {"convert", "try_convert", "dateadd", "datediff", "datediff_big", "datepart",
	"datename", "datetrunc", "date_bucket", NULL};
static const char *const xml_methods[] = {"value", "query", "exist", "nodes", "modify", NULL};
/* rowset functions whose arguments name a table and its columns, or are text */
static const char *const rowset_tables[] = {"containstable", "freetexttable", "semantickeyphrasetable", "semanticsimilaritytable",
	"semanticsimilaritydetailstable", "changetable", "openquery", "openrowset", "opendatasource", NULL};

/* the query f opened by ( at i is EXISTS's operand: its select list
   uses nothing (the server lists no column of its *) */
static void exists_operand(P *p, size_t i, int f)
{
	if (f >= 0 && i && punct(p, i, '(') && is(p, i - 1, "exists")) p->res->b[f].nout = 0;
}

/* scans [a, z) for names: subqueries become blocks seeing blk */
static void expr(P *p, size_t a, size_t z, int blk)
{
	for (size_t i = a; i < z && !p->oom;) {
		const T *t = &p->t[i];
		if (punct(p, i, '(') && (is(p, i + 1, "select") || cte_start(p, i + 1))) {
			int f;
			query(p, i + 1, blk, &f, -1);
			exists_operand(p, i, f);
			i = t->m > 0 ? (size_t)t->m + 1 : z;
			continue;
		}
		if (p->res->text[t->s] == '$' && t->e - t->s > 1) { i++; continue; }   /* $action, $identity */
		if (t->k == TQ_VAR && p->res->text[t->s] == '@' && punct(p, i + 1, '.') && word_or_ident(p, i + 2) && !punct(p, i + 3, '(')) {   /* @t.c: Msg 137; @x.value(..) is a method */
			ref(p, p->t[i + 2].s, p->t[i + 2].e, RF_COLUMN, 137, -1, -1);
			i += 3;
			continue;
		}
		if (is(p, i, "collate") || (punct(p, i, '$')) || (t->k == TQ_OP && t->e - t->s == 2 && p->res->text[t->s] == ':')) { i += 2; continue; }
		if (is(p, i, "next") && is(p, i + 1, "value") && is(p, i + 2, "for")) {   /* a sequence, not a column */
			Name q[4]; int nq; size_t last;
			i = word_or_ident(p, i + 3) ? chain(p, i + 3, q, &nq, &last) : i + 3;
			continue;
		}
		if ((word_or_ident(p, i) && (t->k == TQ_IDENT || !t->rsv)) || (t->k == TQ_VAR && p->res->text[t->s] == '#' && punct(p, i + 1, '.'))) {   /* #t.c */
			Name q[4]; int nq; size_t last, j = chain(p, i, q, &nq, &last);
			if (punct(p, j, '(')) {
				/* a call: a type or date part first is no column */
				size_t close = p->t[j].m > 0 ? (size_t)p->t[j].m : z;
				/* a method on a column: XML's by name; any other (hierarchyid's
				   GetAncestor, geometry's STAsText) when a source names the
				   column's qualifier, so x.c.m() is no schema x's function c.m */
				if (nq >= 2 && (any(p, last, xml_methods) || (nq >= 3 && find_src(p, blk, q, nq - 2) >= 0))) colref(p, i, last - 2, q, nq - 1, blk, NULL, NULL);
				if (nq == 1 && any(p, i, type_first)) {
					size_t k = j + 1;
					while (k < close && !(punct(p, k, ',') && p->t[k].d == p->t[j].d + 1)) k++;
					expr(p, k, close, blk);
					i = close + 1;
					continue;
				}
				if (nq == 1 && (is(p, i, "cast") || is(p, i, "try_cast"))) {
					size_t k = j + 1;
					while (k < close && !(is(p, k, "as") && p->t[k].d == p->t[j].d + 1)) k++;
					expr(p, j + 1, k, blk);
					i = close + 1;
					continue;
				}
				i = j + 1;
				continue;
			}
			colref(p, i, last, q, nq, blk, NULL, NULL);
			i = j;
			continue;
		}
		i++;
	}
}

/* ---- FROM ---- */

static size_t from(P *p, size_t i, size_t z, int blk);
static const char *const join_words[] = {"join", "inner", "left", "right", "full", "cross", "outer", "on", "apply", "where",
	"group", "having", "order", "union", "except", "intersect", "option", "for", "window", "pivot", "unpivot", "with",
	"tablesample", "into", "go", "output", "using", "when", NULL};

/* an alias after a source at i: [AS] name; returns the token after it */
static size_t alias(P *p, size_t i, int sc)
{
	if (p->target) return i;
	if (is(p, i, "as")) i++;
	else if (!name_tok(p, i) || any(p, i, join_words) || starts_statement(p, i)) return i;
	if (!word_or_ident(p, i) && p->t[i].k != TQ_STRING) return i;
	p->res->src[sc].ns = p->t[i].s, p->res->src[sc].ne = p->t[i].e, p->res->src[sc].aliased = 1;
	ref(p, p->t[i].s, p->t[i].e, RF_ALIAS, RS_OK, sc, -1);
	return i + 1;
}
/* a column list ( a, b ) at i into source sc */
static size_t col_list(P *p, size_t i, int sc)
{
	Res *r = p->res;
	size_t z = (size_t)p->t[i].m;
	r->src[sc].list = 1, r->src[sc].cols = (uint32_t)r->nnames;
	for (size_t k = i + 1; k < z; k++) {
		if (!word_or_ident(p, k) || !GROW(r->names, r->nnames, p->ncap)) continue;
		r->names[r->nnames++] = (Out){p->t[k].s, p->t[k].e, NULL, -1, -1};
		r->src[sc].ncols++;
		/* OPENJSON WITH (name type 'path' ..): the name is first in each item */
		while (k + 1 < z && !punct(p, k + 1, ',')) k = punct(p, k + 1, '(') && p->t[k + 1].m > 0 ? (size_t)p->t[k + 1].m : k + 1;
	}
	return z + 1;
}

static long object(P *p, const Name *q, int nq)
{
	const Snap *s = p->res->snap;
	if (nq > 3) return -1;
	return snap_object(s, nq >= 2 ? q[nq - 2] : (Name){0}, q[nq - 1]);
}

/* the columns of a derived table or CTE sc need names, all different
   (Msg 8155, 8156), as many as its column list says (8158, 8159); the
   error goes on reference ref_at, its name */
static void check_cols(P *p, int sc, size_t ref_at)
{
	Res *r = p->res;
	Src *s = &r->src[sc];
	size_t n = src_ncols(r, (size_t)sc);
	if (n == SIZE_MAX || ref_at >= r->nr) return;
	if (s->list && s->inner >= 0 && r->b[s->inner].nout != s->ncols) { r->r[ref_at].status = r->b[s->inner].nout > s->ncols ? 8158 : 8159; return; }
	for (size_t i = 0; i < n; i++) {
		Name a = out_name(r, src_out(r, (size_t)sc, i));
		if (!a.s) { r->r[ref_at].status = 8155; return; }
		for (size_t j = 0; j < i; j++) {
			Name b = out_name(r, src_out(r, (size_t)sc, j));
			if (snap_eq(r->snap, a, b)) { r->r[ref_at].status = 8156; return; }
		}
	}
}

/* the exposed name of source sc is another's in its block */
static void same_name(P *p, int sc, size_t ref_at)
{
	Res *r = p->res;
	const Src *s = &r->src[sc];
	for (size_t i = 0; i < (size_t)sc && s->ne; i++)
		if (r->src[i].blk == s->blk && !r->src[i].hidden && r->src[i].ne && snap_eq(r->snap, nm_span(p, r->src[i].ns), nm_span(p, s->ns)))
			/* two tables named alike: 1013; an alias or a CTE twice: 1011 */
			r->r[ref_at].status = s->kind == SRC_TABLE && r->src[i].kind == SRC_TABLE && !s->aliased && !r->src[i].aliased ? RS_SAMENAME : RS_SAMEALIAS;
}

/* one table source at i in block blk; lateral: it sees the sources
   before it (APPLY), else only the blocks outside (rule 2, 4) */
static size_t source(P *p, size_t i, size_t z, int blk, int lateral)
{
	Res *r = p->res;
	int sees = lateral ? blk : r->b[blk].parent, sc;
	if (punct(p, i, '(')) {
		size_t close = p->t[i].m > 0 ? (size_t)p->t[i].m : z;
		int f = -1;
		if (is(p, i + 1, "values")) expr(p, i + 2, close, sees);
		else if (query(p, i + 1, sees, &f, -1), f < 0) {
			/* ( a JOIN b ON .. ): its sources are this block's */
			from(p, i + 1, close, blk);
			return close + 1;
		}
		sc = new_src(p, SRC_DERIVED, blk);
		if (sc < 0) return z;
		r->src[sc].inner = f;
		r->src[sc].s = p->t[i].s;
		i = alias(p, close + 1, sc);
		if (punct(p, i, '(') && p->t[i].m > 0) i = col_list(p, i, sc);
		r->src[sc].e = p->t[i - 1].e;
		if (r->src[sc].ne) same_name(p, sc, r->nr - 1), check_cols(p, sc, r->nr - 1);
		return i;
	}
	if (p->t[i].k == TQ_VAR) {
		/* a temp table or table variable this text made, else one made
		   elsewhere (a caller's #t): unknown columns.  A table variable
		   has no name a qualifier can use (Msg 137) until aliased */
		int d = -1;
		for (size_t k = p->nloc; k-- > 0 && d < 0;)
			if (snap_eq(r->snap, nm_span(p, r->src[p->loc[k].src].ns), p->t[i].nm)) d = p->loc[k].src;
		sc = new_src(p, d >= 0 ? r->src[d].kind : SRC_UNKNOWN, blk);
		if (sc < 0) return z;
		r->src[sc].s = p->t[i].s;
		if (r->text[p->t[i].s] == '#') r->src[sc].ns = p->t[i].s, r->src[sc].ne = p->t[i].e;
		if (d >= 0) r->src[sc].inner = r->src[d].inner, r->src[sc].obj = r->src[d].obj;
		ref(p, p->t[i].s, p->t[i].e, RF_CTE, d >= 0 ? RS_OK : RS_UNSURE, sc, d);
		i = alias(p, i + 1, sc);
		if (is(p, i, "with") && punct(p, i + 1, '(') && p->t[i + 1].m > 0) i = (size_t)p->t[i + 1].m + 1;   /* hints */
		r->src[sc].e = p->t[i - 1].e;
		return i;
	}
	if (!word_or_ident(p, i)) return i + 1;
	Name q[4];
	int nq;
	size_t last, j = chain(p, i, q, &nq, &last);
	int call = !p->target && punct(p, j, '(');   /* INSERT t (cols): no call */
	long o = -1;
	int cte = -1;
	if (nq == 1 && !call)
		for (size_t k = p->ncte; k-- > 0;)
			if (snap_eq(r->snap, nm_span(p, r->src[p->cte[k].src].ns), q[0])) { cte = p->cte[k].src; break; }
	if (cte < 0) o = object(p, q, nq);
	/* a trigger's inserted and deleted: its table's columns; the name is
	   no object's, so it is marked as an alias */
	int pseudo = o < 0 && cte < 0 && nq == 1 && !call && p->trig >= 0 && (is(p, i, "inserted") || is(p, i, "deleted"));
	if (pseudo) o = r->src[p->trig].obj;
	sc = new_src(p, cte >= 0 ? SRC_CTE : o >= 0 ? SRC_TABLE : SRC_UNKNOWN, blk);
	if (sc < 0) return z;
	if (cte >= 0) {
		Src d = r->src[cte];
		r->src[sc].inner = d.inner, r->src[sc].list = d.list, r->src[sc].cols = d.cols, r->src[sc].ncols = d.ncols;
		ref(p, p->t[i].s, p->t[last].e, RF_CTE, RS_OK, sc, cte);
	} else {
		r->src[sc].obj = (int32_t)o;
		/* not in the snapshot by design: builtin rowset functions, system
		   views, other databases and servers, a trigger's inserted and
		   deleted (slice 4): unknown columns */
		int outside = call || nq > 3 || (nq >= 2 && (snap_eq(r->snap, q[nq - 2], (Name){"sys", 3, 0}) || snap_eq(r->snap, q[nq - 2], (Name){"information_schema", 18, 0})))
			|| (nq == 1 && (is(p, i, "inserted") || is(p, i, "deleted")));
		ref(p, p->t[i].s, p->t[last].e, pseudo ? RF_ALIAS : RF_OBJECT, o >= 0 ? RS_OK : outside ? RS_UNSURE : RS_NOOBJ, sc, -1);
		if (o >= 0 && !strchr("UVSI", r->snap->o[o].type[0]) && strcmp(r->snap->o[o].type, "TF")) r->src[sc].kind = SRC_UNKNOWN;
	}
	r->src[sc].s = p->t[i].s;
	if (!call || o >= 0) r->src[sc].ns = p->t[last].s, r->src[sc].ne = p->t[last].e;   /* OPENJSON(..) has no name until aliased */
	if (pseudo) r->src[sc].aliased = 2;
	if (call) {
		size_t close = p->t[j].m > 0 ? (size_t)p->t[j].m : z;
		if (nq >= 2 && any(p, last, xml_methods)) colref(p, i, last - 2, q, nq - 1, sees, NULL, NULL);   /* x.col.nodes('..') */
		if (!(nq == 1 && any(p, i, rowset_tables))) expr(p, j + 1, close, sees);
		else if ((is(p, i, "containstable") || is(p, i, "freetexttable")) && word_or_ident(p, j + 1)) {
			/* (table, column | (columns) | *, ..): the columns are the table's */
			Name t[4]; int nt; size_t tl, k = chain(p, j + 1, t, &nt, &tl);
			long to = object(p, t, nt);
			int ts = to >= 0 ? new_src(p, SRC_TABLE, -1) : -1;
			ref(p, p->t[j + 1].s, p->t[tl].e, RF_OBJECT, to >= 0 ? RS_OK : RS_NOOBJ, ts, -1);
			if (ts >= 0) r->src[ts].obj = (int32_t)to;
			size_t a = k + 1, e = punct(p, a, '(') && p->t[a].m > 0 ? (size_t)p->t[a].m : a + 1;
			for (a += punct(p, a, '('); ts >= 0 && a < e; a++) if (word_or_ident(p, a)) col_of(p, a, ts);
		}
		j = close + 1;
		if (is(p, j, "with") && punct(p, j + 1, '(') && p->t[j + 1].m > 0) {   /* OPENJSON .. WITH (cols) */
			r->src[sc].kind = SRC_DERIVED;
			j = col_list(p, j + 1, sc);
		}
	}
	if (is(p, j, "for") && is(p, j + 1, "system_time")) {   /* AS OF x | FROM x TO y | BETWEEN x AND y | CONTAINED IN (..) | ALL */
		j += 2;
		if (is(p, j, "as") && is(p, j + 1, "of")) expr(p, j + 2, j + 3, sees), j += 3;
		else if (is(p, j, "from") || is(p, j, "between")) expr(p, j + 1, j + 2, sees), expr(p, j + 3, j + 4, sees), j += 4;
		else if (is(p, j, "contained") && punct(p, j + 2, '(') && p->t[j + 2].m > 0) j = (size_t)p->t[j + 2].m + 1;
		else if (is(p, j, "all")) j++;
	}
	i = alias(p, j, sc);
	if (punct(p, i, '(') && p->t[i].m > 0 && r->src[sc].kind != SRC_TABLE && !p->target) i = col_list(p, i, sc);
	if (is(p, i, "with") && punct(p, i + 1, '(') && p->t[i + 1].m > 0) i = (size_t)p->t[i + 1].m + 1;   /* hints */
	if (is(p, i, "tablesample") && punct(p, i + 1, '(') && p->t[i + 1].m > 0) {
		i = (size_t)p->t[i + 1].m + 1;
		if (is(p, i, "repeatable") && punct(p, i + 1, '(')) i = (size_t)p->t[i + 1].m + 1;
	}
	r->src[sc].e = p->t[i - 1].e;
	same_name(p, sc, r->nr - 1);
	return i;
}

/* the FROM list [i, z) of block blk */
static size_t from(P *p, size_t i, size_t z, int blk)
{
	Res *r = p->res;
	int lateral = 0, grp = p->grp, ob = p->on_blk, og = p->on_grp;
	p->grp = 0;
	while (i < z && !p->oom) {
		if (lateral) p->on_blk = blk, p->on_grp = p->grp;
		i = source(p, i, z, blk, lateral);
		p->on_blk = ob, p->on_grp = og;
		lateral = 0;
		while (i < z && (is(p, i, "pivot") || is(p, i, "unpivot")) && punct(p, i + 1, '(')) {
			/* PIVOT (agg(c) FOR c IN ([new], ..)), UNPIVOT (new FOR new IN (c, ..)):
			   the names in a PIVOT's IN list and before an UNPIVOT's IN are
			   new columns, not references */
			size_t close = p->t[i + 1].m > 0 ? (size_t)p->t[i + 1].m : z, in = i + 2;
			while (in < close && !(is(p, in, "in") && p->t[in].d == p->t[i + 1].d + 1)) in++;
			if (is(p, i, "pivot")) expr(p, i + 2, in, blk);
			else expr(p, in + 1, close, blk);
			for (size_t k = 0; k < r->nsrc; k++) if (r->src[k].blk == blk && r->src[k].grp == p->grp) r->src[k].hidden = 1;
			int sc = new_src(p, SRC_UNKNOWN, blk);
			if (sc < 0) return z;
			r->src[sc].s = p->t[i].s;
			i = alias(p, close + 1, sc);
			r->src[sc].e = p->t[i - 1].e;
		}
		if (is(p, i, "on")) {
			size_t k = i + 1;
			while (k < z && !(p->t[k].d == p->t[i].d && (punct(p, k, ',') || any(p, k, join_words)) && !is(p, k, "on"))) k++;
			p->on_blk = blk, p->on_grp = p->grp;
			expr(p, i + 1, k, blk);
			p->on_blk = ob, p->on_grp = og;
			i = k;
		}
		if (punct(p, i, ',')) { i++, p->grp++; continue; }
		if (is(p, i, "cross") || is(p, i, "outer")) {
			if (is(p, i + 1, "apply")) { lateral = 1, i += 2; continue; }
		}
		size_t k = i;
		while (k < z && any(p, k, (const char *const[]){"inner", "left", "right", "full", "outer", "cross", "loop", "hash", "merge", "remote", NULL})) k++;
		if (is(p, k, "join")) { i = k + 1; continue; }
		if (i < z) i++;
	}
	p->grp = grp;
	return i;
}

/* ---- SELECT ---- */

/* the select list item [a, z) of block blk */
static void item(P *p, size_t a, size_t z, int blk)
{
	Res *r = p->res;
	if (a >= z) return;
	if (punct(p, a, '*') && a + 1 == z) {   /* every visible source's columns, in FROM order */
		ref(p, p->t[a].s, p->t[a].e, RF_STAR, RS_OK, -1, -1);
		for (size_t s = 0; s < r->nsrc; s++) {
			if (r->src[s].blk != blk || r->src[s].hidden) continue;
			size_t n = src_ncols(r, s);
			if (n == SIZE_MAX) { r->r[r->nr - 1].status = RS_UNSURE; continue; }
			for (size_t c = 0; c < n; c++) {
				long k;
				Name nm = src_col(r, s, c, &k);
				const Out *so = k < 0 ? src_out(r, s, c) : NULL;
				out(p, blk, (Out){so ? so->ns : 0, so ? so->ne : 0, k >= 0 ? r->snap->c[k].name : so ? so->cat : NULL, (int)s, (int)c});
				(void)nm;
			}
		}
		return;
	}
	if (z >= a + 3 && punct(p, z - 1, '*') && punct(p, z - 2, '.')) {   /* x.* */
		Name q[4]; int nq; size_t last;
		chain(p, a, q, &nq, &last);
		int s = find_src(p, blk, q, nq);
		ref(p, p->t[a].s, p->t[last].e, RF_QUAL, s < 0 ? RS_UNBOUND : RS_OK, s, -1);
		ref(p, p->t[z - 1].s, p->t[z - 1].e, RF_STAR, s < 0 ? RS_UNBOUND : RS_OK, s, -1);
		size_t n = s < 0 ? 0 : src_ncols(r, (size_t)s);
		if (n == SIZE_MAX) { r->r[r->nr - 1].status = RS_UNSURE; return; }
		for (size_t c = 0; c < n; c++) {
			long k;
			src_col(r, (size_t)s, c, &k);
			const Out *so = k < 0 ? src_out(r, (size_t)s, c) : NULL;
			out(p, blk, (Out){so ? so->ns : 0, so ? so->ne : 0, k >= 0 ? r->snap->c[k].name : so ? so->cat : NULL, s, (int)c});
		}
		return;
	}
	if (p->t[a].k == TQ_VAR && punct(p, a + 1, '=')) { expr(p, a + 2, z, blk); return; }   /* @v = expr */
	if ((word_or_ident(p, a) || p->t[a].k == TQ_STRING) && punct(p, a + 1, '=') && (p->t[a].k != TQ_WORD || !p->t[a].rsv)) {
		ref(p, p->t[a].s, p->t[a].e, RF_OUT_DEF, RS_OK, -1, (int)r->b[blk].nout);
		size_t n0 = r->nr;
		expr(p, a + 2, z, blk);
		int src = -1, col = -1;
		if (r->nr == n0 + 1 && r->r[n0].kind == RF_COLUMN && r->r[n0].status == RS_OK && tok_at(p, r->r[n0].s) == a + 2 && tok_at(p, r->r[n0].e) == z) src = r->r[n0].src, col = r->r[n0].col;
		out(p, blk, (Out){p->t[a].s, p->t[a].e, NULL, src, col});
		return;
	}
	/* expr [AS] alias: an alias after a name, ), a literal or END */
	size_t e = z, ns = 0, ne = 0;
	if (z - a >= 2 && (word_or_ident(p, z - 1) || p->t[z - 1].k == TQ_STRING) && (p->t[z - 1].k != TQ_WORD || !p->t[z - 1].rsv) && !punct(p, z - 2, '.')) {
		size_t b = z - 2;
		if (is(p, b, "as")) e = b, ns = p->t[z - 1].s, ne = p->t[z - 1].e;
		else if (name_tok(p, b) || punct(p, b, ')') || p->t[b].k == TQ_STRING || p->t[b].k == TQ_NUMBER || p->t[b].k == TQ_VAR || is(p, b, "end") || is(p, b, "null"))
			e = z - 1, ns = p->t[z - 1].s, ne = p->t[z - 1].e;
	}
	if (ne) ref(p, ns, ne, RF_OUT_DEF, RS_OK, -1, (int)r->b[blk].nout);
	size_t n0 = r->nr;
	expr(p, a, e, blk);
	int src = -1, col = -1;
	/* a lone column: its name and origin (rule: an unaliased expression has no name) */
	int lone = r->nr == n0 + 1 + (n0 < r->nr && r->r[n0].kind == RF_QUAL) && r->r[r->nr - 1].kind == RF_COLUMN && tok_at(p, r->r[n0].s) == a && tok_at(p, r->r[r->nr - 1].e) == e;
	if (r->nr == n0 + 2 && r->r[n0].kind != RF_QUAL) lone = 0;
	if (lone && r->r[r->nr - 1].status == RS_OK) src = r->r[r->nr - 1].src, col = r->r[r->nr - 1].col;
	if (!ne && lone) ns = r->r[r->nr - 1].s, ne = r->r[r->nr - 1].e;
	out(p, blk, (Out){ns, ne, NULL, src, col});
}

static const char *const clauses[] = {"from", "where", "group", "having", "into", "window", NULL};

/* column definitions ( name type .., CONSTRAINT .., PRIMARY KEY (..),
   INDEX .. ) at i: a block whose outputs are the column names */
static int col_defs(P *p, size_t i)
{
	static const char *const not_col[] = {"constraint", "primary", "unique", "check", "index", "foreign", "period", NULL};
	size_t z = (size_t)p->t[i].m, base = p->npend;
	int b = new_blk(p, -1, i);
	if (b < 0) return -1;
	for (size_t k = i + 1; k < z; k++) {
		if (word_or_ident(p, k) && !any(p, k, not_col)) out(p, b, (Out){p->t[k].s, p->t[k].e, NULL, -1, -1});
		while (k < z && !(punct(p, k, ',') && p->t[k].d == p->t[i].d + 1)) k++;
	}
	p->res->b[b].e = p->t[z].e;
	commit(p, b, base);
	return b;
}

/* a temp table or table variable named at token i, its columns block
   blk's outputs; later names see it (rule 14) */
static void local(P *p, size_t i, int blk, long obj)
{
	int sc = new_src(p, obj >= 0 ? SRC_TABLE : SRC_CTE, -1);
	if (sc < 0 || !GROW(p->loc, p->nloc, p->loccap)) return;
	Src *s = &p->res->src[sc];
	s->ns = s->s = p->t[i].s, s->ne = s->e = p->t[i].e, s->inner = blk, s->obj = (int32_t)obj;
	ref(p, p->t[i].s, p->t[i].e, RF_CTE_DEF, RS_OK, sc, -1);
	p->loc[p->nloc++] = (Cte){sc};
}

/* SELECT at i: one block seeing parent; returns where it ends */
static size_t select_(P *p, size_t i, int parent, int *blk)
{
	Res *r = p->res;
	int b = *blk = new_blk(p, parent, i);
	size_t base = p->npend;
	if (b < 0) return p->nt;
	int32_t d = p->t[i].d;
	size_t at[6], z = i + 1;
	for (int k = 0; k < 6; k++) at[k] = SIZE_MAX;
	for (; z < p->nt && p->t[z].d >= d; z++) {
		if (p->t[z].d != d) continue;
		if (punct(p, z, ')') || ends_query(p, z) || starts_statement(p, z)) break;
		for (int k = 0; clauses[k]; k++)
			if (is(p, z, clauses[k]) && at[k] == SIZE_MAX && (k != 2 || is(p, z + 1, "by"))) at[k] = z;   /* not WITHIN GROUP */
	}
	r->b[b].e = p->t[z - 1].e;
	/* each clause runs to the next one present */
	size_t end[6];
	for (int k = 0; k < 6; k++) {
		end[k] = z;
		for (int m = 0; m < 6; m++) if (at[m] != SIZE_MAX && at[k] != SIZE_MAX && at[m] > at[k] && at[m] < end[k]) end[k] = at[m];
	}
	size_t list = i + 1, list_end = z;
	for (int k = 0; k < 6; k++) if (at[k] != SIZE_MAX && at[k] < list_end) list_end = at[k];
	if (at[0] != SIZE_MAX) from(p, at[0] + 1, end[0], b);
	/* ALL, DISTINCT, TOP (n) [PERCENT] [WITH TIES] */
	if (is(p, list, "all") || is(p, list, "distinct")) list++;
	if (is(p, list, "top")) {
		list++;
		if (punct(p, list, '(') && p->t[list].m > 0) { expr(p, list + 1, (size_t)p->t[list].m, parent); list = (size_t)p->t[list].m + 1; }
		else list++;
		if (is(p, list, "percent")) list++;
		if (is(p, list, "with") && is(p, list + 1, "ties")) list += 2;
	}
	for (size_t a = list, k = list; k <= list_end; k++)
		if (k == list_end || (punct(p, k, ',') && p->t[k].d == d)) item(p, a, k, b), a = k + 1;
	if (at[1] != SIZE_MAX) expr(p, at[1] + 1, end[1], b);
	if (at[2] != SIZE_MAX) expr(p, at[2] + 1, end[2], b);
	if (at[3] != SIZE_MAX) expr(p, at[3] + 1, end[3], b);
	if (at[4] != SIZE_MAX && p->t[at[4] + 1].k == TQ_VAR) local(p, at[4] + 1, b, -1);   /* SELECT .. INTO #t */
	commit(p, b, base);
	return z;
}

/* ORDER BY items [a, z): a lone name is an output of ob first (rule 7);
   with a set operator only outputs count */
static void order_by(P *p, size_t a, size_t z, int ob, int setop)
{
	Res *r = p->res;
	for (size_t s = a, k = a; k <= z; k++) {
		if (k < z && !(punct(p, k, ',') && p->t[k].d == p->t[a].d)) continue;
		size_t e = k;
		while (e > s && (is(p, e - 1, "asc") || is(p, e - 1, "desc"))) e--;
		if (e == s + 1 && word_or_ident(p, s) && (p->t[s].k == TQ_IDENT || !p->t[s].rsv)) {
			int hit = -1, n = 0;
			for (uint32_t o = 0; o < r->b[ob].nout; o++) {
				Name nm = out_name(r, &r->out[r->b[ob].out + o]);
				if (nm.s && snap_eq(r->snap, nm, p->t[s].nm)) hit = hit < 0 ? (int)o : hit, n++;   /* two: 209, even the same column */
			}
			if (n) { ref(p, p->t[s].s, p->t[s].e, RF_OUT, n > 1 ? RS_AMBIG : RS_OK, -1, hit); s = k + 1; continue; }
		}
		if (setop) {
			/* SQL Server: Msg 104, the item must be in the select list */
			size_t n0 = r->nr;
			expr(p, s, e, ob);
			for (size_t m = n0; m < r->nr; m++) if (r->r[m].kind == RF_COLUMN && r->r[m].status == RS_OK) r->r[m].status = RS_UNSURE;
		} else expr(p, s, e, ob);
		s = k + 1;
	}
}

/* a query at i ([WITH ..] term {set-op term} [ORDER BY ..]): returns
   where it ends; *first: its first block, -1 when i is no query.  cte:
   the CTE whose body it is, or -1: a recursive CTE's later parts see it
   with its first part's columns (rule 3) */
static size_t query(P *p, size_t i, int parent, int *first, int cte)
{
	Res *r = p->res;
	size_t ncte = p->ncte;
	*first = -1;
	if (cte_start(p, i)) {
		i++;
		for (;;) {
			int sc = new_src(p, SRC_CTE, -1);
			if (sc < 0 || !GROW(p->cte, p->ncte, p->ctecap)) return p->nt;
			r->src[sc].ns = p->t[i].s, r->src[sc].ne = p->t[i].e, r->src[sc].s = p->t[i].s;
			int twice = 0;   /* a name this WITH defined already: Msg 239 */
			for (size_t k = ncte; k < p->ncte; k++) twice |= snap_eq(r->snap, nm_span(p, r->src[p->cte[k].src].ns), p->t[i].nm);
			ref(p, p->t[i].s, p->t[i].e, RF_CTE_DEF, twice ? RS_SAMECTE : RS_OK, sc, -1);
			p->cte[p->ncte++] = (Cte){sc};
			i++;
			if (punct(p, i, '(') && p->t[i].m > 0) i = col_list(p, i, sc);
			if (!is(p, i, "as") || !punct(p, i + 1, '(') || p->t[i + 1].m < 0) return i;
			int f;
			size_t close = (size_t)p->t[i + 1].m;
			size_t at = r->nr - 1;
			query(p, i + 2, parent, &f, sc);
			r->src[sc].inner = f;
			r->src[sc].e = p->t[close].e;
			check_cols(p, sc, at);
			i = close + 1;
			if (!punct(p, i, ',')) break;
			i++;
		}
	}
	if (cte >= 0 || !dml_at(p, i)) ;
	else return i = dml(p, i, parent), p->ncte = ncte, i;
	int setop = 0;
	for (;;) {
		int b;
		size_t z;
		if (punct(p, i, '(') && p->t[i].m > 0) {
			query(p, i + 1, parent, &b, -1);
			if (b < 0) break;
			z = (size_t)p->t[i].m + 1;
		} else if (is(p, i, "select")) z = select_(p, i, parent, &b);
		else break;
		if (*first < 0 && (*first = b, cte >= 0)) r->src[cte].inner = b;
		i = z;
		if (is(p, i, "union") || is(p, i, "except") || is(p, i, "intersect")) {
			setop = 1;
			i += 1 + is(p, i + 1, "all");
			continue;
		}
		break;
	}
	if (*first >= 0 && is(p, i, "order") && is(p, i + 1, "by")) {
		size_t a = i + 2, z = a;
		int32_t d = p->t[i].d;
		while (z < p->nt && p->t[z].d >= d && !(p->t[z].d == d && (punct(p, z, ')') || is(p, z, "offset") || ends_query(p, z) || starts_statement(p, z)))) z++;
		order_by(p, a, z, *first, setop);
		i = z;
		if (is(p, i, "offset")) {
			while (i < p->nt && p->t[i].d >= d && !(p->t[i].d == d && (punct(p, i, ')') || ends_query(p, i) || (starts_statement(p, i) && !is(p, i, "fetch"))))) i++;
		}
	}
	if (is(p, i, "for") && any(p, i + 1, for_clause)) {
		int32_t d = p->t[i].d;
		while (i < p->nt && !(p->t[i].d < d || (p->t[i].d == d && (punct(p, i, ')') || is(p, i, "option") || starts_statement(p, i))))) i++;
	}
	if (is(p, i, "option") && punct(p, i + 1, '(') && p->t[i + 1].m > 0) i = (size_t)p->t[i + 1].m + 1;
	p->ncte = ncte;
	return i;
}


/* ---- INSERT, UPDATE, DELETE, MERGE (rules 15 to 19) ---- */

static const char *const dml_words[] = {"insert", "update", "delete", "merge", NULL};
/* a DML statement starts at i: not GRANT UPDATE, ON DELETE CASCADE, AFTER
   INSERT, FOR UPDATE, IF UPDATE(c), INNER MERGE JOIN */
static int dml_at(const P *p, size_t i)
{
	static const char *const not_after[] = {"on", "for", "after", "of", "grant", "deny", "revoke", "inner", "left", "right",
		"full", "outer", "cross", NULL};
	return any(p, i, dml_words) && !punct(p, i + 1, '(') && !punct(p, i + 1, ',') && !(i && (any(p, i - 1, not_after) || punct(p, i - 1, ',')));
}

/* where a clause starting at i ends: at depth d, a word of stop, a ; or
   (stmts) a statement; MERGE's clauses hold UPDATE SET and INSERT */
static size_t clause_end(const P *p, size_t i, int32_t d, const char *const *stop, int stmts)
{
	for (; i < p->nt && p->t[i].d >= d; i++)
		if (p->t[i].d == d && (any(p, i, stop) || punct(p, i, ';') || (stmts && starts_statement(p, i)))) break;
	return i;
}

/* TOP (n) [PERCENT] at i, its expression seeing blk */
static size_t top(P *p, size_t i, int blk)
{
	if (!is(p, i, "top") || !punct(p, i + 1, '(') || p->t[i + 1].m < 0) return i;
	expr(p, i + 2, (size_t)p->t[i + 1].m, blk);
	return (size_t)p->t[i + 1].m + 1 + is(p, (size_t)p->t[i + 1].m + 1, "percent");
}

/* the last reference is a column written; one written twice since
   reference from is Msg 264 */
static void wrote(P *p, size_t from)
{
	Ref *w = &p->res->r[p->res->nr - 1];
	w->wr = 1;
	for (size_t m = from; w->status == RS_OK && m + 1 < p->res->nr; m++)
		if (p->res->r[m].wr && p->res->r[m].src == w->src && p->res->r[m].col == w->col) w->status = 264;
}

/* column names (c, ..) at i, written, in source sc */
static size_t written(P *p, size_t i, int sc)
{
	size_t z = (size_t)p->t[i].m, n0 = p->res->nr;
	for (size_t k = i + 1; k < z; k++) {
		if (!word_or_ident(p, k)) continue;
		col_of(p, k, sc);
		wrote(p, n0);
	}
	return z + 1;
}

/* the target named at i, a source of blk: with a FROM (from), a name its
   sources expose, or the one reference to that table there (Msg 8154
   when two have aliases and none is bare); else a source of its own.
   *z: the token after it */
static int target(P *p, size_t i, int blk, int from, size_t *z)
{
	Res *r = p->res;
	Name q[4];
	int nq, sc = -1, n = 0, bare = 0;
	size_t last = i;
	*z = p->t[i].k == TQ_VAR ? i + 1 : chain(p, i, q, &nq, &last);
	if (from && p->t[i].k == TQ_VAR) q[0] = p->t[i].nm, nq = 1;
	if (from) {
		long o = nq == 1 ? -1 : object(p, q, nq);
		for (size_t s = 0; s < r->nsrc; s++) {
			const Src *x = &r->src[s];
			if (x->blk != blk || x->aliased == 2) continue;
			if (nq == 1 && x->ne && snap_eq(r->snap, nm_span(p, x->ns), q[0])) { sc = (int)s, n = 1; break; }
			if (nq == 1 && x->kind == SRC_TABLE && !x->ne) continue;
			if (x->kind == SRC_TABLE && (nq == 1 ? snap_eq(r->snap, (Name){r->snap->o[x->obj].name, strlen(r->snap->o[x->obj].name), 1}, q[0]) : x->obj == o)) {
				if (!x->aliased) { if (!bare++) sc = (int)s, n = 1; }
				else if (!bare) sc = (int)s, n++;
			}
		}
		if (sc >= 0) {
			ref(p, p->t[i].s, p->t[last].e, r->src[sc].aliased && nq == 1 && !bare ? RF_QUAL : RF_OBJECT, n > 1 ? 8154 : RS_OK, sc, -1);
			if (is(p, *z, "with") && punct(p, *z + 1, '(') && p->t[*z + 1].m > 0) *z = (size_t)p->t[*z + 1].m + 1;
			return sc;
		}
	}
	p->target = 1;
	*z = source(p, i, p->nt, blk, 0);
	p->target = 0;
	return (int)r->nsrc - 1;
}

/* OUTPUT items [INTO t [(c, ..)]], at most twice, from i: inserted and
   deleted are target sc's columns, the statement's sources blk visible */
static size_t output(P *p, size_t i, int blk, int sc, int32_t d)
{
	Res *r = p->res;
	while (is(p, i, "output")) {
		static const char *const stop[] = {"into", "output", "from", "where", "option", "when", NULL};
		size_t z = clause_end(p, i + 1, d, stop, 1);
		int ob = new_blk(p, blk, i);
		if (ob < 0) return p->nt;
		/* the pseudo tables, named by their first mention */
		for (size_t k = i + 1; k < z; k++) {
			if (!(is(p, k, "inserted") || is(p, k, "deleted")) || !punct(p, k + 1, '.')) continue;
			int seen_it = 0;
			for (size_t s = 0; s < r->nsrc; s++) if (r->src[s].blk == ob && snap_eq(r->snap, nm_span(p, r->src[s].ns), p->t[k].nm)) seen_it = 1;
			if (seen_it) continue;
			int ps = new_src(p, r->src[sc].kind, ob);
			if (ps < 0) return p->nt;
			Src *x = &r->src[ps];
			x->obj = r->src[sc].obj, x->inner = r->src[sc].inner, x->list = r->src[sc].list, x->cols = r->src[sc].cols, x->ncols = r->src[sc].ncols;
			x->ns = x->s = p->t[k].s, x->ne = x->e = p->t[k].e, x->aliased = 2;
		}
		for (size_t a = i + 1, k = a; k <= z; k++)
			if (k == z || (punct(p, k, ',') && p->t[k].d == d)) expr(p, a, k, ob), a = k + 1;
		r->b[ob].e = p->t[z - 1].e;
		i = z;
		if (is(p, i, "into")) {
			int t = target(p, i + 1, ob, 0, &i);
			if (punct(p, i, '(') && p->t[i].m > 0) i = written(p, i, t);
		}
	}
	return i;
}

/* SET items [i, z): col = expr, col += expr, @v = expr, @v = col = expr,
   col.method(..); a bare column is the target's (rule 16) */
static void set_list(P *p, size_t i, size_t z, int blk, int sc)
{
	size_t set0 = p->res->nr;
	for (size_t a = i, k = i; k <= z; k++) {
		if (k < z && !(punct(p, k, ',') && p->t[k].d == p->t[i].d)) continue;
		size_t e = a;
		while (e + 1 < k && p->t[e].k == TQ_VAR && punct(p, e + 1, '=')) e += 2;
		Name q[4]; int nq; size_t last, j = e < k && word_or_ident(p, e) ? chain(p, e, q, &nq, &last) : e;
		if (j > e && nq == 2 && punct(p, j, '(') && p->t[j].m > 0) {   /* c.modify(..), c.WRITE(..): c is written */
			col_of(p, e, sc);
			wrote(p, set0);
			e = j;
		} else if (j > e && j < k && p->t[j].k == TQ_OP && !punct(p, j, '(')) {
			size_t n0 = p->res->nr;
			if (nq == 1) col_of(p, e, sc);
			else colref(p, e, last, q, nq, blk, NULL, NULL);
			if (p->res->nr > n0) wrote(p, set0);
			e = j;
		}
		expr(p, e, k, blk);
		a = k + 1;
	}
}

/* the statement at i (dml_at), its subqueries seeing parent */
static size_t dml(P *p, size_t i, int parent)
{
	Res *r = p->res;
	int32_t d = p->t[i].d;
	int b = new_blk(p, parent, i), sc;
	if (b < 0) return p->nt;
	size_t j, z;
	if (is(p, i, "update") || is(p, i, "delete")) {
		static const char *const stop[] = {"set", "output", "from", "where", "option", NULL};
		int upd = is(p, i, "update");
		size_t tg = top(p, i + 1, parent);
		if (!upd && is(p, tg, "from")) tg++;
		if (!(name_tok(p, tg) || p->t[tg].k == TQ_VAR)) return i + 1;
		size_t at[5];   /* set, output, from, where, option */
		for (int k = 0; k < 5; k++) at[k] = SIZE_MAX;
		for (j = tg + 1; j < p->nt && p->t[j].d >= d; j++) {
			if (p->t[j].d != d) continue;
			int k = 0;
			while (stop[k] && !is(p, j, stop[k])) k++;
			if (stop[k] && at[k] == SIZE_MAX && (k || upd)) { at[k] = j; continue; }
			if (starts_statement(p, j)) break;
		}
		z = j;
		if (upd && at[0] == SIZE_MAX) return i + 1;   /* UPDATE STATISTICS and the like */
		size_t end[5];
		for (int k = 0; k < 5; k++) {
			end[k] = z;
			for (int m = 0; m < 5; m++) if (at[m] != SIZE_MAX && at[k] != SIZE_MAX && at[m] > at[k] && at[m] < end[k]) end[k] = at[m];
		}
		if (at[2] != SIZE_MAX) from(p, at[2] + 1, end[2], b);
		sc = target(p, tg, b, at[2] != SIZE_MAX, &j);
		if (at[0] != SIZE_MAX) set_list(p, at[0] + 1, end[0], b, sc);
		if (at[1] != SIZE_MAX) output(p, at[1], b, sc, d);
		if (at[3] != SIZE_MAX) expr(p, at[3] + 1, end[3], b);
	} else if (is(p, i, "insert")) {
		j = top(p, i + 1, parent);
		j += is(p, j, "into");
		if (!(name_tok(p, j) || p->t[j].k == TQ_VAR)) return i + 1;
		sc = target(p, j, b, 0, &j);
		if (punct(p, j, '(') && p->t[j].m > 0) j = written(p, j, sc);
		j = output(p, j, b, sc, d);
		/* VALUES and the query see the blocks outside, not the target */
		if (is(p, j, "values")) {
			for (j++; punct(p, j, '(') && p->t[j].m > 0; j += 1 + punct(p, (size_t)p->t[j].m + 1, ',')) {
				expr(p, j + 1, (size_t)p->t[j].m, parent);
				j = (size_t)p->t[j].m;
			}
		} else if (is(p, j, "select") || cte_start(p, j) || punct(p, j, '(')) {
			int f;
			j = query(p, j, parent, &f, -1);
			if (f >= 0 && GROW(r->top, r->ntop, p->tcap)) r->top[r->ntop++] = f;
		}
		z = clause_end(p, j, d, (const char *const[]){NULL}, 1);
	} else {   /* MERGE */
		j = top(p, i + 1, parent);
		j += is(p, j, "into");
		if (!(name_tok(p, j) || p->t[j].k == TQ_VAR)) return i + 1;
		sc = target(p, j, b, 0, &j);
		if (is(p, j, "as")) j++;
		if (name_tok(p, j) && !is(p, j, "using")) {   /* its alias: after the hints, unlike a FROM's */
			r->src[sc].ns = p->t[j].s, r->src[sc].ne = p->t[j].e, r->src[sc].aliased = 1;
			ref(p, p->t[j].s, p->t[j].e, RF_ALIAS, RS_OK, sc, -1);
			r->src[sc].e = p->t[j++].e;
		}
		if (!is(p, j, "using")) return i + 1;
		j = source(p, j + 1, p->nt, b, 0);   /* sees the blocks outside, not the target */
		int ss = (int)r->nsrc - 1;
		static const char *const stop[] = {"when", "output", "option", NULL};
		size_t k = clause_end(p, j + 1, d, stop, 0);
		if (is(p, j, "on")) expr(p, j + 1, k, b);
		/* WHEN MATCHED sees both; NOT MATCHED [BY TARGET] the source only;
		   NOT MATCHED BY SOURCE the target only */
		for (j = k; is(p, j, "when"); j = k) {
			int not = is(p, j + 1, "not"), by_src = not && is(p, j + 3, "by") && is(p, j + 4, "source");
			size_t then = j;
			while (then < p->nt && !is(p, then, "then")) then++;
			k = clause_end(p, then + 1, d, stop, 0);
			if (not) r->src[by_src ? ss : sc].hidden = 1;
			size_t and = j;
			while (and < then && !is(p, and, "and")) and++;
			size_t n0 = r->nr;
			if (and < then) expr(p, and + 1, then, b);
			/* the search condition naming the side it cannot see, or any
			   name it cannot bind: Msg 5333 (NOT MATCHED), 5334 (BY SOURCE) */
			int hid = by_src ? ss : sc;
			for (size_t m = n0; not && m < r->nr; m++) {
				Ref *x = &r->r[m];
				if (x->status == RS_NOCOL   /* even a name no source has */
				    || (x->kind == RF_QUAL && x->status == RS_UNBOUND && r->src[hid].ne && snap_eq(r->snap, nm_span(p, r->src[hid].ns), nm_span(p, x->s))))
					x->status = by_src ? 5334 : 5333, m += x->kind == RF_QUAL && m + 1 < r->nr ? (r->r[m + 1].status = x->status, 1) : 0;
			}
			size_t a = then + 1;
			if (is(p, a, "update") && is(p, a + 1, "set")) set_list(p, a + 2, k, b, sc);
			else if (is(p, a, "insert")) {
				r->src[sc].hidden = 0;
				a++;
				if (punct(p, a, '(') && p->t[a].m > 0) a = written(p, a, sc);
				if (not) r->src[sc].hidden = 1;
				if (is(p, a, "values") && punct(p, a + 1, '(') && p->t[a + 1].m > 0) expr(p, a + 2, (size_t)p->t[a + 1].m, b);
			}
			r->src[ss].hidden = r->src[sc].hidden = 0;
		}
		j = output(p, j, b, sc, d);
		z = clause_end(p, j, d, (const char *const[]){NULL}, 1);
	}
	r->b[b].e = p->t[z - 1].e;
	return z;
}

/* ---- the text ---- */

/* the significant tokens; depth counts parentheses and CASE .. END, so a
   clause word inside either is not the block's.  A ( has the depth
   outside it, its ) the depth inside, so a ) at a block's own depth is
   the one that ends the block. */
static int lex(P *p)
{
	Res *r = p->res;
	Text *tx = text_new(r->text, r->n);
	size_t cap = 0, sp = 0;
	struct { int32_t tok; char c; } *st = malloc((r->n + 1) * sizeof *st);
	p->low = malloc(r->n + 1), p->unq = malloc(r->n + 1);
	if (!tx || !st || !p->low || !p->unq) { text_free(tx), free(st); return -1; }
	for (size_t i = 0; i < r->n; i++) p->low[i] = r->text[i] >= 'A' && r->text[i] <= 'Z' ? r->text[i] + 32 : r->text[i];
	TsqlRd rd = {.t = tx};
	char *u = p->unq;
	for (size_t pos = 0; pos < r->n; ) {
		int k;
		size_t e = tsql_token(&rd, pos, &k);
		if (e <= pos) e = pos + 1;
		if (k != TQ_SPACE && k != TQ_COMMENT && GROW(p->t, p->nt, cap)) {
			T t = {(uint32_t)pos, (uint32_t)e, (uint8_t)k, 0, -1, (int32_t)sp, {r->text + pos, e - pos, 0}};
			if (k == TQ_WORD) t.rsv = (tsql_word(r->text + pos, e - pos, NULL) & TQ_RECASE) != 0;
			if (k == TQ_IDENT || k == TQ_STRING) t.nm = snap_name(r->text + pos, e - pos, u), u += t.nm.n;
			p->t[p->nt] = t;
			if (k == TQ_PUNCT && r->text[pos] == '(') st[sp].tok = (int32_t)p->nt, st[sp++].c = '(';
			else if (k == TQ_PUNCT && r->text[pos] == ')') {
				while (sp && st[sp - 1].c != '(') sp--;   /* a CASE left open */
				if (sp) sp--, p->t[st[sp].tok].m = (int32_t)p->nt, p->t[p->nt].m = st[sp].tok;
			} else if (k == TQ_WORD && e - pos == 4 && !memcmp(p->low + pos, "case", 4)) st[sp].tok = -1, st[sp++].c = 'c';
			else if (k == TQ_WORD && e - pos == 3 && !memcmp(p->low + pos, "end", 3) && sp && st[sp - 1].c == 'c') sp--;
			p->nt++;
		}
		pos = e;
	}
	text_free(tx), free(st);
	return p->oom ? -1 : 0;
}

static int by_start(const void *a, const void *b)
{
	uint32_t x = ((const Ref *)a)->s, y = ((const Ref *)b)->s;
	return (x > y) - (x < y);
}

int resolve(Res *res, const Snap *snap, const char *text, size_t n)
{
	P *p = calloc(1, sizeof *p);
	memset(res, 0, sizeof *res);
	if (!p) return -1;
	res->snap = snap, res->text = text, res->n = n, res->priv = p;
	p->on_blk = -1, p->trig = -1;
	p->res = res;
	if (lex(p) < 0) return -1;
	/* queries start wherever they start: SELECT, WITH, ( SELECT inside IF
	   EXISTS, SET @x = ( .. ) and the like */
	for (size_t i = 0; i < p->nt && !p->oom;) {
		int f;
		/* DECLARE @t TABLE (..), CREATE TABLE #t (..): a block of its column names */
		size_t v = i + 1 + is(p, i + 1, "table"), o = v + 1 + is(p, v + 1, "as") + is(p, v + 1, "table") + (is(p, v + 1, "as") && is(p, v + 2, "table"));
		if (((is(p, i, "declare") && is(p, o - 1, "table")) || (is(p, i, "create") && v == i + 2)) && p->t[v].k == TQ_VAR && punct(p, o, '(') && p->t[o].m > 0) {
			local(p, v, col_defs(p, o), -1);
			i = (size_t)p->t[o].m + 1;
			continue;
		}
		/* a parameter @x schema.type READONLY: the table type's columns */
		if (p->t[i].k == TQ_VAR && word_or_ident(p, i + 1)) {
			Name q[4]; int nq; size_t last, j = chain(p, i + 1, q, &nq, &last);
			long o = is(p, j, "readonly") ? object(p, q, nq) : -1;
			if (o >= 0) { local(p, i, -1, o); i = j + 1; continue; }
		}
		/* CREATE [OR ALTER] TRIGGER x ON t: inserted and deleted are t's */
		if (is(p, i, "trigger") && i && (is(p, i - 1, "create") || is(p, i - 1, "alter"))) {
			Name q[4]; int nq; size_t last, j = word_or_ident(p, i + 1) ? chain(p, i + 1, q, &nq, &last) : i + 1;
			long o = is(p, j, "on") && word_or_ident(p, j + 1) ? (chain(p, j + 1, q, &nq, &last), object(p, q, nq)) : -1;
			if (o >= 0 && (p->trig = new_src(p, SRC_TABLE, -1)) >= 0) res->src[p->trig].obj = (int32_t)o;
		}
		if (is(p, i, "update") && punct(p, i + 1, '(') && p->trig >= 0 && word_or_ident(p, i + 2)) col_of(p, i + 2, p->trig);   /* a trigger's IF UPDATE(c) */
		if (dml_at(p, i)) { size_t z = dml(p, i, -1); i = z > i ? z : i + 1; continue; }
		if (is(p, i, "select") || cte_start(p, i)) {
			size_t z = query(p, i, -1, &f, -1);
			if (i) exists_operand(p, i - 1, f);   /* IF EXISTS (SELECT ..) */
			if (f >= 0 && GROW(res->top, res->ntop, p->tcap)) res->top[res->ntop++] = f;
			i = z > i ? z : i + 1;
		} else i++;
	}
	/* in text order, which the walk is not (a block's outputs come after
	   its FROM): refactors edit front to back and compare texts in order */
	if (res->nr) qsort(res->r, res->nr, sizeof *res->r, by_start);
	return p->oom ? -1 : 0;
}

void resolve_free(Res *res)
{
	P *p = res->priv;
	if (p) free(p->t), free(p->low), free(p->unq), free(p->cte), free(p->loc), free(p->pend), free(p);
	free(res->r), free(res->src), free(res->b), free(res->out), free(res->names), free(res->top);
	memset(res, 0, sizeof *res);
}
