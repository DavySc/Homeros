/* Refactors over resolved text (refactor.h).  Each computes edits, then
   refactor_same() resolves the edited text again and refuses unless every
   name means what it meant: one check for all refactors, so a rule the
   refactor forgot (a qualifier an inner source would capture, say) shows
   up as a refusal, never as changed meaning. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include "refactor.h"
#include "tsql.h"

static int grow(void **a, size_t *cap, size_t need, size_t size)
{
	if (need <= *cap) return 1;
	size_t c = *cap ? *cap : 64;
	while (c < need) c *= 2;
	void *q = realloc(*a, c * size);
	if (!q) return 0;
	*a = q, *cap = c;
	return 1;
}
static int put(Edits *ed, const char *s, size_t n)
{
	if (!grow((void **)&ed->buf, &ed->bcap, ed->nbuf + n + 1, 1)) return -1;
	memcpy(ed->buf + ed->nbuf, s, n);
	ed->nbuf += n;
	ed->buf[ed->nbuf] = 0;
	return 0;
}
#define PUTS(ed, lit) put(ed, lit, sizeof lit - 1)
/* a name as T-SQL needs it written: bare when it is a regular identifier
   and no reserved word, else [..] with ] doubled */
static int put_name(Edits *ed, Name nm)
{
	int bare = nm.n > 0 && nm.n < 128;
	for (size_t i = 0; i < nm.n && bare; i++) {
		char c = nm.s[i];
		bare = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || (i && ((c >= '0' && c <= '9') || c == '@' || c == '#' || c == '$'));
	}
	if (bare && (tsql_word(nm.s, nm.n, NULL) & TQ_RECASE)) bare = 0;
	if (bare) return put(ed, nm.s, nm.n);
	if (put(ed, "[", 1)) return -1;
	for (size_t i = 0; i < nm.n; i++)
		if (put(ed, nm.s + i, 1) || (nm.s[i] == ']' && put(ed, "]", 1))) return -1;
	return put(ed, "]", 1);
}
/* the token at byte a is the word w */
static int word(const Res *r, uint32_t a, const char *w)
{
	uint32_t s, z;
	size_t n = strlen(w);
	return res_tok(r, a, 1, &s, &z) && s == a && z - s == n && !strncasecmp(r->text + a, w, n);
}

/* replace [a, z) by what was put since mark */
static int edit(Edits *ed, uint32_t a, uint32_t z, size_t mark)
{
	if (!grow((void **)&ed->e, &ed->ecap, ed->ne + 1, sizeof *ed->e)) return -1;
	ed->e[ed->ne++] = (Edit){a, z, (uint32_t)mark, (uint32_t)(ed->nbuf - mark)};
	return 0;
}
static int refuse(Edits *ed, const char *why) { ed->why = why; return -2; }


/* the exposed name of source sc, as written */
static int put_exposed(Edits *ed, const Res *r, int sc)
{
	const Src *s = &r->src[sc];
	return put(ed, r->text + s->ns, s->ne - s->ns);
}

/* the innermost block holding byte pos */
static int block_at(const Res *r, uint32_t pos)
{
	int best = -1;
	for (size_t b = 0; b < r->nb; b++)
		if (r->b[b].s <= pos && pos < r->b[b].e && (best < 0 || r->b[b].s >= r->b[best].s)) best = (int)b;
	return best;
}

/* the columns of source sc as "x.c1, x.c2" */
static int put_cols(Edits *ed, const Res *r, int sc, int *first)
{
	size_t n = src_ncols(r, (size_t)sc);
	if (n == SIZE_MAX) return refuse(ed, "a source's columns are not known (temp table, table variable, rowset function)");
	if (!r->src[sc].ne) return refuse(ed, "a source no qualifier can name (OPENJSON or a table variable, with no alias)");
	for (size_t i = 0; i < n; i++) {
		long k;
		Name nm = src_col(r, (size_t)sc, i, &k);
		if (!nm.s) return refuse(ed, "a derived table's column has no name");
		if ((!*first && put(ed, ", ", 2)) || put_exposed(ed, r, sc) || put(ed, ".", 1) || put_name(ed, nm)) return -1;
		*first = 0;
	}
	return 0;
}

/* the query at pos is EXISTS (..)'s operand, whose select list means
   nothing: its * stays (a comment between them hides that; then the *
   is written out, which is still correct) */
static int in_exists(const Res *r, uint32_t pos)
{
	const char *t = r->text;
	while (pos && strchr(" \t\r\n", t[pos - 1])) pos--;
	if (!pos || t[--pos] != '(') return 0;
	while (pos && strchr(" \t\r\n", t[pos - 1])) pos--;
	return pos >= 6 && !strncasecmp(t + pos - 6, "exists", 6) && (pos == 6 || !(isalnum((unsigned char)t[pos - 7]) || t[pos - 7] == '_'));
}

int refactor_star(const Res *r, size_t a, size_t z, Edits *ed)
{
	for (size_t i = 0; i < r->nr; i++) {
		const Ref *x = &r->r[i];
		int b = block_at(r, x->s);
		if (x->kind != RF_STAR || x->s < a || x->s >= z || b < 0 || in_exists(r, r->b[b].s)) continue;
		if (x->status != RS_OK) return refuse(ed, "a * whose sources are not all known");
		size_t mark = ed->nbuf;
		int first = 1, rc;
		uint32_t from = x->s;
		if (x->src >= 0) {   /* x.* : the qualifier is the reference before */
			from = r->r[i - 1].s;
			if ((rc = put_cols(ed, r, x->src, &first))) return rc;
		} else {
			for (size_t s = 0; s < r->nsrc; s++)
				if (r->src[s].blk == b && (rc = put_cols(ed, r, (int)s, &first))) return rc;
		}
		if (edit(ed, from, x->e, mark)) return -1;
	}
	return 0;
}

int refactor_qualify(const Res *r, size_t a, size_t z, Edits *ed)
{
	for (size_t i = 0; i < r->nr; i++) {
		const Ref *x = &r->r[i];
		if (x->kind != RF_COLUMN || x->s < a || x->s >= z) continue;
		if ((x->s && r->text[x->s - 1] == '.') || x->status == RS_UNSURE || x->wr || (x->status == RS_OK && !r->src[x->src].ne)) continue;   /* left as it is: qualified already; not known; written (SET c =, INSERT t (c)), where a qualifier is mostly not allowed; from a source with no name (OPENJSON, @t, a trigger's UPDATE(c)) */
		if (x->status != RS_OK) return refuse(ed, x->status == RS_AMBIG ? "an ambiguous column" : "a column that does not resolve");
		size_t mark = ed->nbuf;
		if (put_exposed(ed, r, x->src) || put(ed, ".", 1) || edit(ed, x->s, x->s, mark)) return -1;
	}
	return 0;
}

/* x is a * over a table holding snapshot column col or its twin (a
   derived table's or CTE's columns keep their names: see below) */
static int star_has(const Res *r, const Ref *x, long col, long twin)
{
	for (size_t s = 0; s < r->nsrc; s++) {
		const Src *c = &r->src[s];
		if (c->kind != SRC_TABLE || (x->src >= 0 ? (int)s != x->src : c->blk != block_at(r, x->s))) continue;
		long k = (long)r->snap->o[c->obj].col;
		if ((col >= k && col < k + r->snap->o[c->obj].ncol) || (twin >= k && twin < k + r->snap->o[c->obj].ncol)) return 1;
	}
	return 0;
}

/* a name a rename would change appears where the resolver did not read
   it as a known name: a reference that does not resolve (a temp table
   made elsewhere), a call it took for a function (c.ToString()), a string
   (dynamic SQL).  The rename must not guess. */
#define UNREAD "the name appears where it is not known (a temp table made elsewhere, a method call, a string): rename it by hand"
static int unread(const Res *r, Name nm)
{
	char buf[512];
	size_t k = 0;   /* refs are in text order: k walks along */
	for (uint32_t a, z, pos = 0; res_tok(r, pos, 1, &a, &z); pos = z) {
		if (z - a >= sizeof buf || !snap_eq(r->snap, snap_name(r->text + a, z - a, buf), nm)) continue;
		while (k < r->nr && r->r[k].e <= a) k++;
		int known = 0;
		for (size_t i = k; i < r->nr && r->r[i].s <= a; i++) known |= r->r[i].status == RS_OK && r->r[i].e >= z;
		if (!known) return 1;
	}
	return 0;
}

/* why the server would not rename column col, or NULL */
static const char *blocked(const Snap *s, long col)
{
	for (size_t o = 0; o < s->no; o++)
		if (s->o[o].hist == s->c[col].obj + 1) return "a history table's column: rename the current table's";
	return s->c[col].block;
}

/* output o's name is seen: a statement's result, a view's or function's
   columns, or a derived table's or CTE's columns (not a subquery's under
   IN, EXISTS or a comparison, whose one value has no name) */
static int named_out(const Res *r, size_t o)
{
	size_t b = 0;
	while (b < r->nb && !(r->b[b].out <= o && o < r->b[b].out + r->b[b].nout)) b++;
	for (size_t t = 0; t < r->ntop; t++) if ((size_t)r->top[t] == b) return 1;
	for (size_t s = 0; s < r->nsrc; s++) if (r->src[s].inner == (int32_t)b && !r->src[s].list) return 1;
	return 0;
}

int refactor_rename_col(const Res *r, long col, Name to, Edits *ed)
{
	const Snap *sn = r->snap;
	const SnapCol *c = &sn->c[col];
	long twin = snap_twin(sn, (size_t)col);
	if (blocked(sn, col)) return refuse(ed, blocked(sn, col));
	if (unread(r, (Name){c->name, strlen(c->name), 1})) return refuse(ed, UNREAD);
	for (size_t i = 0; i < r->nr; i++) {
		const Ref *x = &r->r[i];
		if (x->kind == RF_STAR && x->status == RS_OK && !in_exists(r, r->b[block_at(r, x->s)].s) && star_has(r, x, col, twin)) return refuse(ed, "a * includes the column (its output name would change): expand it first");
		if (x->kind != RF_COLUMN || x->status != RS_OK) continue;
		const Src *s = &r->src[x->src];
		long k = s->kind == SRC_TABLE ? (long)sn->o[s->obj].col + x->col : -1;
		if (k < 0 || (k != col && k != twin)) continue;
		size_t mark = ed->nbuf;
		if (put_name(ed, to)) return -1;
		size_t o = 0;   /* a lone column naming an output someone sees (a * over it copies the name: the first is its own) */
		while (o < r->nout && !(r->out[o].ns == x->s && r->out[o].ne == x->e)) o++;
		if (o < r->nout && named_out(r, o) && (PUTS(ed, " AS ") || put(ed, r->text + x->s, x->e - x->s))) return -1;
		if (edit(ed, x->s, x->e, mark)) return -1;
	}
	return 0;
}

/* s as a T-SQL N'..' string */
static int put_str(Edits *ed, const char *s, size_t n)
{
	if (put(ed, "N'", 2)) return -1;
	for (size_t i = 0; i < n; i++)
		if (put(ed, s + i, 1) || (s[i] == '\'' && put(ed, "'", 1))) return -1;
	return put(ed, "'", 1);
}

int refactor_rename_script(const Snap *s, const Snap *after, long col, Name to, Edits *ed)
{
	const SnapCol *c = &s->c[col];
	const SnapObj *t = &s->o[c->obj];
	Edits q = {0};
	if (blocked(s, col)) return refuse(ed, blocked(s, col));
	if (PUTS(&q, "")) return -1;   /* q.buf: [schema].[table].[column] */
	if (put_name(&q, (Name){t->schema, strlen(t->schema), 1}) || PUTS(&q, ".") || put_name(&q, (Name){t->name, strlen(t->name), 1}) || PUTS(&q, ".") || put_name(&q, (Name){c->name, strlen(c->name), 1})) { edits_free(&q); return -1; }
	int rc = PUTS(ed, "SET XACT_ABORT ON;\nBEGIN TRANSACTION;\nEXEC sp_rename ") || put_str(ed, q.buf, q.nbuf) || PUTS(ed, ", ") || put_str(ed, to.s, to.n) || PUTS(ed, ", 'COLUMN';\n") ? -1 : 0;
	edits_free(&q);
	for (size_t o = 0; o < s->no && !rc; o++) {
		const char *text = s->o[o].text;
		if (!text) continue;
		size_t n = strlen(text), len;
		Res r;
		Edits m = {0};
		uint32_t a, z;
		if (resolve(&r, s, text, n) < 0) return -1;
		rc = res_tok(&r, 0, 1, &a, &z) && word(&r, a, "create") ? (PUTS(&m, "ALTER") || edit(&m, a, z, 0) ? -1 : refactor_rename_col(&r, col, to, &m)) : refuse(&m, "a module that does not start with CREATE");
		resolve_free(&r);
		char *u = !rc && m.ne > 1 ? edits_apply(&m, text, n, &len) : NULL;
		if (rc == -2) {   /* the module's name, then why, kept in ed->buf */
			snprintf(ed->msg, sizeof ed->msg, "%s.%s: %s", s->o[o].schema, s->o[o].name, m.why);
			rc = refuse(ed, ed->msg);
		} else if (u && !refactor_same(s, text, n, after, u, len)) {
			snprintf(ed->msg, sizeof ed->msg, "%s.%s: the meaning would change", s->o[o].schema, s->o[o].name);
			rc = refuse(ed, ed->msg);
		} else if (u && (PUTS(ed, "EXEC (") || put_str(ed, u, len) || PUTS(ed, ");\n"))) rc = -1;
		free(u);
		edits_free(&m);
	}
	return rc ? rc : PUTS(ed, "COMMIT;\n");
}

/* the CTE definition a source uses, or -1 */
static int cte_of(const Res *r, int sc)
{
	for (size_t i = 0; i < r->nr; i++) if (r->r[i].kind == RF_CTE && r->r[i].src == sc) return r->r[i].col;
	return -1;
}

/* the reference whose bytes hold pos (the innermost: the last) */
static const Ref *ref_at(const Res *r, size_t pos)
{
	const Ref *x = NULL;
	for (size_t i = 0; i < r->nr; i++)
		if (r->r[i].s <= pos && pos < r->r[i].e) x = &r->r[i];
	return x;
}

/* the CTE a reference names (its definition, a use, or a use's name as a
   qualifier), or -1 */
static int cte_named(const Res *r, const Ref *x)
{
	return x->kind == RF_CTE_DEF ? x->src : x->kind == RF_CTE ? x->col
	     : x->kind == RF_QUAL && r->src[x->src].aliased != 1 ? cte_of(r, x->src) : -1;
}

/* CTE d's text: its column list [*cs, *ce) (empty when none) and its body's
   parentheses *open, *close; 0 when it is not name [(cols)] AS (..) */
static int cte_text(const Res *r, int d, uint32_t *cs, uint32_t *ce, uint32_t *open, uint32_t *close)
{
	uint32_t a, z;
	*cs = *ce = r->src[d].ne;
	if (!res_tok(r, *ce, 1, &a, &z)) return 0;
	if (r->text[a] == '(') {
		if ((*ce = res_match(r, a)) == UINT32_MAX || !res_tok(r, ++*ce, 1, &a, &z)) return 0;
		*cs = r->src[d].ne;
	}
	return word(r, a, "as") && res_tok(r, z, 1, open, &z) && r->text[*open] == '(' && (*close = res_match(r, *open)) != UINT32_MAX;
}

/* put what bytes [a, z) of the text are */
static int put_text(Edits *ed, const Res *r, uint32_t a, uint32_t z) { return put(ed, r->text + a, z - a); }

int refactor_inline_cte(const Res *r, size_t pos, Edits *ed)
{
	const Ref *x = ref_at(r, pos);
	int d = x && x->src >= 0 && x->status == RS_OK ? cte_named(r, x) : -1;
	uint32_t cs, ce, open, close, a, z, ds, de;
	if (d < 0 || strchr("#@", r->text[r->src[d].s])) return refuse(ed, "no CTE here");
	if (!cte_text(r, d, &cs, &ce, &open, &close)) return refuse(ed, "a CTE this cannot read");
	/* the definition goes with the comma after it, or (the last) before it,
	   or (the only one) with WITH */
	ds = r->src[d].ns, de = close + 1;
	if (res_tok(r, de, 1, &a, &z) && r->text[a] == ',') de = res_tok(r, z, 1, &a, &z) ? a : z;
	else if (res_tok(r, ds, -1, &a, &z) && (r->text[a] == ',' || word(r, a, "with"))) {
		ds = a;
		if (r->text[a] != ',' && res_tok(r, de, 1, &a, &z)) de = a;
	}
	size_t mark = ed->nbuf;
	if (edit(ed, ds, de, mark)) return -1;
	for (size_t i = 0; i < r->nr; i++) {
		const Ref *y = &r->r[i];
		if (y->kind != RF_CTE || y->col != d) continue;
		if (y->s > open && y->s < close) return refuse(ed, "a recursive CTE");
		int al = r->src[y->src].aliased == 1;
		if (al && ce > cs) return refuse(ed, "a CTE with a column list, used with an alias");
		mark = ed->nbuf;
		if (PUTS(ed, "(") || put_text(ed, r, open + 1, close) || PUTS(ed, ")") || (!al && (PUTS(ed, " ") || put_text(ed, r, r->src[d].ns, r->src[d].ne) || put_text(ed, r, cs, ce))) || edit(ed, y->s, y->e, mark)) return -1;
	}
	return 0;
}

int refactor_extract_cte(const Res *r, size_t pos, Edits *ed)
{
	const Ref *x = ref_at(r, pos);
	const Src *s = x && (x->kind == RF_ALIAS || x->kind == RF_QUAL) && x->src >= 0 ? &r->src[x->src] : NULL;
	uint32_t close = s ? res_match(r, s->s) : UINT32_MAX, at = UINT32_MAX, cs, ce, open, cl, a, z;
	if (!s || s->kind != SRC_DERIVED || close == UINT32_MAX) return refuse(ed, "no derived table's name here");
	if (!res_tok(r, s->s + 1, 1, &a, &z) || !word(r, a, "select")) return refuse(ed, "a CTE's body is a SELECT (not VALUES, not a nested WITH)");
	/* where the CTE goes: before the CTE whose body holds it, else after
	   the statement's last CTE, else as a new WITH before the statement */
	int before = 0, root = s->blk;
	for (size_t i = 0; i < r->nr && at == UINT32_MAX; i++) {
		const Ref *y = &r->r[i];
		if (y->kind != RF_CTE_DEF || y->s > pos || strchr("#@", r->text[y->s]) || !cte_text(r, y->src, &cs, &ce, &open, &cl)) continue;
		if (pos < cl) at = y->s, before = 1;
		else if (res_tok(r, cl + 1, 1, &a, &z) && r->text[a] != ',' && pos > cl) {   /* the WITH list ends: is this its statement? */
			while (r->b[root].parent >= 0) root = r->b[root].parent;
			if (r->b[root].s > cl) at = cl + 1;
		}
	}
	while (r->b[root].parent >= 0) root = r->b[root].parent;
	int top = 0;
	for (size_t t = 0; t < r->ntop; t++) top |= r->top[t] == root;
	if (at == UINT32_MAX && !top) return refuse(ed, "a derived table outside a query statement");
	int upper = isupper((unsigned char)r->text[r->b[root].s]);
	size_t mark = ed->nbuf;
	int with = at == UINT32_MAX;
	if (with) {   /* before the statement (an INSERT's, if the query is its source) */
		uint32_t k = res_starter(r, at = r->b[root].s);
		if (k != UINT32_MAX && word(r, k, "insert")) at = k;
		/* WITH follows a ; unless it starts a body (AS, BEGIN); IF, WHILE
		   and ELSE take one statement, which WITH cannot be (Msg 319) */
		if (res_tok(r, at, -1, &a, &z) && r->text[a] != ';' && !word(r, a, "as") && !word(r, a, "begin")) {
			k = res_starter(r, at);
			if (word(r, a, "else") || (k != UINT32_MAX && (word(r, k, "if") || word(r, k, "while")))) return refuse(ed, "a WITH cannot follow IF, WHILE or ELSE without BEGIN");
			if (PUTS(ed, ";")) return -1;
		}
		if (put(ed, upper ? "WITH " : "with ", 5)) return -1;
	} else if (!before && PUTS(ed, ", ")) return -1;
	if (put_text(ed, r, s->ns, s->ne) || put_text(ed, r, s->ne, s->e) || put(ed, upper ? " AS " : " as ", 4) || put_text(ed, r, s->s, close + 1) || (before ? PUTS(ed, ", ") : !with ? 0 : PUTS(ed, " ")) || edit(ed, at, at, mark)) return -1;
	mark = ed->nbuf;
	return put_text(ed, r, s->ns, s->ne) || edit(ed, s->s, s->e, mark) ? -1 : 0;
}

int refactor_rename_at(const Res *r, size_t pos, Name to, Edits *ed)
{
	const Ref *x = ref_at(r, pos);
	if (!x || x->src < 0 || x->status != RS_OK) return refuse(ed, "no alias or CTE name here");
	int al = (x->kind == RF_ALIAS || x->kind == RF_QUAL) && r->src[x->src].aliased == 1 ? x->src : -1, d = al < 0 ? cte_named(r, x) : -1;   /* the aliased source, or the CTE definition */
	if (al < 0 && d < 0) return refuse(ed, "renames here: an alias or a CTE name");
	char buf[512];
	if (x->e - x->s < sizeof buf && unread(r, snap_name(r->text + x->s, x->e - x->s, buf))) return refuse(ed, UNREAD);
	if (d >= 0 && strchr("#@", r->text[r->src[d].s])) return refuse(ed, "a temp table or table variable outlives the statement");
	for (size_t i = 0; i < r->nr; i++) {
		const Ref *y = &r->r[i];
		int hit = al >= 0 ? (y->kind == RF_ALIAS || y->kind == RF_QUAL) && y->src == al
		        : (y->kind == RF_CTE_DEF && y->src == d) || (y->kind == RF_CTE && y->col == d)
		          || (y->kind == RF_QUAL && y->src >= 0 && r->src[y->src].aliased != 1 && cte_of(r, y->src) == d);
		size_t mark = ed->nbuf;
		if (hit && (put_name(ed, to) || edit(ed, y->s, y->e, mark))) return -1;
	}
	return 0;
}

int refactor_info(const Res *r, size_t pos, Edits *ed)
{
	const Ref *x = ref_at(r, pos);
	if (!x || x->src < 0) return refuse(ed, "no known name here");
	const Snap *s = r->snap;
	char line[1024];
	if (x->kind == RF_COLUMN) {
		long k = res_origin(r, x->src, x->col);
		if (k < 0) return refuse(ed, "a computed column");
		const SnapCol *c = &s->c[k];
		const SnapObj *o = &s->o[c->obj];
		snprintf(line, sizeof line, "%s.%s.%s %s%s\n", o->schema, o->name, c->name, c->type, c->nullable ? " null" : " not null");
		if (put(ed, line, strlen(line))) return -1;
		for (size_t f = 0; f < s->nk; f++) {
			const SnapFk *fk = &s->k[f];
			if (fk->from_col == (uint32_t)k || fk->to_col == (uint32_t)k) {
				size_t other = fk->from_col == (uint32_t)k ? fk->to_col : fk->from_col;
				snprintf(line, sizeof line, "  %s %s.%s.%s\n", fk->from_col == (uint32_t)k ? "references" : "referenced by",
				         s->o[s->c[other].obj].schema, s->o[s->c[other].obj].name, s->c[other].name);
				if (put(ed, line, strlen(line))) return -1;
			}
		}
		return 0;
	}
	const Src *sc = &r->src[x->src];
	if (sc->kind != SRC_TABLE) return refuse(ed, "not a table or view");
	const SnapObj *o = &s->o[sc->obj];
	snprintf(line, sizeof line, "%s.%s (%s)\n", o->schema, o->name, o->type);
	if (put(ed, line, strlen(line))) return -1;
	for (uint32_t i = 0; i < o->ncol; i++) {
		const SnapCol *c = &s->c[o->col + i];
		snprintf(line, sizeof line, "  %s %s%s\n", c->name, c->type, c->nullable ? " null" : "");
		if (put(ed, line, strlen(line))) return -1;
	}
	return 0;
}

char *edits_apply(const Edits *ed, const char *text, size_t n, size_t *len)
{
	size_t total = n;
	for (size_t i = 0; i < ed->ne; i++) total = total + ed->e[i].n - (ed->e[i].z - ed->e[i].a);   /* in size_t: an edit may shorten (uint32_t would wrap) */
	char *o = malloc(total + 1);
	if (!o) return NULL;
	size_t w = 0, at = 0;
	for (size_t i = 0; i < ed->ne; i++) {   /* edits come in text order */
		memcpy(o + w, text + at, ed->e[i].a - at), w += ed->e[i].a - at;
		memcpy(o + w, ed->buf + ed->e[i].s, ed->e[i].n), w += ed->e[i].n;
		at = ed->e[i].z;
	}
	memcpy(o + w, text + at, n - at), w += n - at;
	o[w] = 0;
	*len = w;
	return o;
}

/* what each column reference and each statement's output is */
static long origin_of(const Res *r, const Ref *x) { return x->status == RS_OK ? res_origin(r, x->src, x->col) : -1; }

/* sources and blocks of the old text paired with the new's, one to one:
   a refactor may renumber them (inline a CTE, write out a *), never merge
   two (a capture) */
typedef struct { int *x, *y; } Pairs;
static int paired(const Pairs *m, int a, int b) { return a < 0 || b < 0 ? a == b : m->x[a] < 0 ? m->y[b] < 0 : m->x[a] == b; }
static void pair(Pairs *m, int a, int b) { if (a >= 0 && b >= 0) m->x[a] = b, m->y[b] = a; }
/* a column's origin: a catalog column, or a block's computed output */
static int same_origin(const Pairs *blk, long a, long b)
{
	if (a > -2 || b > -2) return a == b;
	return ((-2 - a) & 0xffff) == ((-2 - b) & 0xffff) && paired(blk, (int)((-2 - a) >> 16), (int)((-2 - b) >> 16));
}
static void pair_origin(Pairs *blk, long a, long b) { if (a <= -2 && b <= -2) pair(blk, (int)((-2 - a) >> 16), (int)((-2 - b) >> 16)); }
/* source a of x may be source b of y: the same table (or both derived,
   a CTE's use and a derived table alike), in paired blocks */
static int same_src(const Res *x, const Res *y, const Pairs *src, const Pairs *blk, int a, int b)
{
	if (a < 0 || b < 0) return a == b;
	const Src *p = &x->src[a], *q = &y->src[b];
	return (p->kind == SRC_TABLE) == (q->kind == SRC_TABLE) && (p->kind != SRC_TABLE || p->obj == q->obj) && paired(src, a, b) && paired(blk, p->blk, q->blk);
}

int refactor_same(const Snap *s, const char *a, size_t na, const Snap *t, const char *b, size_t nb)
{
	Res x, y;
	int same = 0;
	if (resolve(&x, s, a, na) < 0) return 0;
	if (resolve(&y, t, b, nb) < 0) { resolve_free(&x); return 0; }
	size_t n = x.nsrc + y.nsrc + x.nb + y.nb;
	int *mem = malloc((n + y.nr + 1) * sizeof *mem), *used = mem + n;   /* used: y's references matched */
	Pairs src = {mem, mem + x.nsrc}, blk = {mem + x.nsrc + y.nsrc, mem + x.nsrc + y.nsrc + x.nb};
	if (!mem || x.ntop != y.ntop) goto done;
	for (size_t i = 0; i < n + y.nr; i++) mem[i] = i < n ? -1 : 0;
	/* every statement: the same output names, from the same columns */
	for (size_t t = 0; t < x.ntop; t++) {
		const Blk *p = &x.b[x.top[t]], *q = &y.b[y.top[t]];
		if (p->nout != q->nout || !paired(&blk, x.top[t], y.top[t])) goto done;
		pair(&blk, x.top[t], y.top[t]);
		for (uint32_t i = 0; i < p->nout; i++) {
			const Out *o = &x.out[p->out + i], *u = &y.out[q->out + i];
			Name m = out_name(&x, o), n = out_name(&y, u);
			long g = res_origin(&x, o->src, o->col), h = res_origin(&y, u->src, u->col);
			if (!m.s != !n.s || (m.s && !snap_eq(s, m, n)) || !same_origin(&blk, g, h)) goto done;
			pair_origin(&blk, g, h);
		}
	}
	/* every column reference of the old text means the same in the new:
	   the same column of the paired source (two sources may be one table,
	   and a renamed alias may capture).  Text may move (a CTE inlined), so
	   the search starts after the last match and wraps; any match is a
	   witness, so a wrong pairing can only refuse.  The new text may have
	   more references (a * written out) but no errors. */
	size_t j = 0;
	for (size_t i = 0; i < y.nr; i++) if (y.r[i].status > RS_UNSURE) goto done;
	for (size_t i = 0; i < x.nr; i++) {
		const Ref *p = &x.r[i];
		if (p->kind != RF_COLUMN) continue;
		size_t k = 0;
		for (; k < y.nr; k++, j = (j + 1) % y.nr) {
			const Ref *q = &y.r[j];
			if (!used[j] && q->kind == RF_COLUMN && q->status == p->status && same_src(&x, &y, &src, &blk, p->src, q->src) && same_origin(&blk, origin_of(&x, p), origin_of(&y, q))) break;
		}
		if (k == y.nr) goto done;
		used[j] = 1;
		pair(&src, p->src, y.r[j].src), pair_origin(&blk, origin_of(&x, p), origin_of(&y, &y.r[j]));
		if (p->src >= 0) pair(&blk, x.src[p->src].blk, y.src[y.r[j].src].blk);
	}
	same = 1;
done:
	free(mem);
	resolve_free(&x), resolve_free(&y);
	return same;
}

void edits_free(Edits *ed)
{
	free(ed->e), free(ed->buf);
	memset(ed, 0, sizeof *ed);
}
