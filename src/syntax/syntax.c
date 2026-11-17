/* Syntax colours from tree-sitter, as Neovim 0.12's highlighter makes them
   (runtime/lua/vim/treesitter/: highlighter.lua, languagetree.lua,
   query.lua): the same grammars, highlights.scm and injections.scm, the same
   predicates and directives, the same match limit, a language tree per
   injected language, and per byte the capture with the highest priority,
   the later one on a tie (later trees after earlier ones).
   An edit shifts every tree at once (ts_tree_edit), so colours stay on their
   text; the reparse is deferred work that stops when a key is waiting and
   resumes on the next call, as Neovim's async parsing does. */
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tree_sitter/api.h>
#include "grid.h"
#include "syntax.h"
#include "lmatch.h"

/* Neovim's standard capture names (:h treesitter-highlight-groups), in
   grid.h's order: name i is face i + 1.  A capture takes the longest of these
   that prefixes it, as a highlight group name falls back to its parent
   ("@keyword.return.c" to "@keyword.return"); one with none, or whose name
   starts with _, colours nothing and so hides nothing below it. */
static const char *const names[] = {
	"variable", "variable.builtin", "variable.parameter", "variable.parameter.builtin",
	"variable.member", "constant", "constant.builtin", "constant.macro", "module",
	"module.builtin", "label", "string", "string.documentation", "string.regexp", "string.escape",
	"string.special", "string.special.symbol", "string.special.path", "string.special.url",
	"character", "character.special", "boolean", "number", "number.float", "type", "type.builtin",
	"type.definition", "attribute", "attribute.builtin", "property", "function",
	"function.builtin", "function.call", "function.macro", "function.method",
	"function.method.call", "constructor", "operator", "keyword", "keyword.coroutine",
	"keyword.function", "keyword.operator", "keyword.import", "keyword.type", "keyword.modifier",
	"keyword.repeat", "keyword.return", "keyword.debug", "keyword.exception",
	"keyword.conditional", "keyword.conditional.ternary", "keyword.directive",
	"keyword.directive.define", "punctuation.delimiter", "punctuation.bracket",
	"punctuation.special", "comment", "comment.documentation", "comment.error", "comment.warning",
	"comment.todo", "comment.note", "markup.strong", "markup.italic", "markup.strikethrough",
	"markup.underline", "markup.heading", "markup.heading.1", "markup.heading.2",
	"markup.heading.3", "markup.heading.4", "markup.heading.5", "markup.heading.6", "markup.quote",
	"markup.math", "markup.link", "markup.link.label", "markup.link.url", "markup.raw",
	"markup.raw.block", "markup.list", "markup.list.checked", "markup.list.unchecked", "diff.plus",
	"diff.minus", "diff.delta", "tag", "tag.builtin", "tag.attribute", "tag.delimiter",
};
enum { NNAMES = sizeof names / sizeof *names, PRIO = 100, CHUNK = 8192, LIMIT = 256 };
#define NONE UINT32_MAX

static unsigned char face_of(const char *cap, uint32_t n)
{
	int best = -1;
	size_t bl = 0;
	for (int i = 0; i < NNAMES; i++) {
		size_t l = strlen(names[i]);
		if (l > bl && l <= n && !memcmp(cap, names[i], l) && (l == n || cap[l] == '.'))
			best = i, bl = l;
	}
	return (unsigned char)(best + 1);   /* FACE_NONE when none */
}

/* Grammars linked in, by file name ending (Neovim's filetype for each). */
const TSLanguage *tree_sitter_c(void);
const TSLanguage *tree_sitter_lua(void);
const TSLanguage *tree_sitter_markdown(void);
const TSLanguage *tree_sitter_markdown_inline(void);
static const struct Lang {
	const char *name, *ext[3];
	const TSLanguage *(*fn)(void);
} langs[] = {
	{ "c", { ".c", ".h" }, tree_sitter_c },
	{ "lua", { ".lua" }, tree_sitter_lua },
	{ "markdown", { ".md" }, tree_sitter_markdown },
	{ "markdown_inline", { NULL }, tree_sitter_markdown_inline },
};
enum { NLANGS = sizeof langs / sizeof *langs };

#ifndef SYNTAX_QUERIES
#define SYNTAX_QUERIES "src/syntax/queries"
#endif

static int lang_of_path(const char *path, size_t pl)
{
	for (int i = 0; i < NLANGS; i++)
		for (int k = 0; k < 3 && langs[i].ext[k]; k++) {
			size_t el = strlen(langs[i].ext[k]);
			if (pl > el && !memcmp(path + pl - el, langs[i].ext[k], el)) return i;
		}
	return -1;
}

enum { P_EQ, P_MATCH, P_CONTAINS, P_ANYOF, P_ANCESTOR, P_PARENT };
typedef struct {
	unsigned char kind, neg, any;
	uint32_t cap, cap2;        /* cap2 != NONE: #eq? @a @b */
	uint32_t nstr;
	const char **str;          /* the query owns the strings */
	uint32_t *len;
} Pred;
/* A pattern's predicates and the metadata its directives set. */
typedef struct {
	Pred *p;
	uint32_t n;
	int prio, cprio;           /* #set! priority N; #set! @pcap priority N */
	uint32_t pcap;
	const char *ilang;         /* #set! injection.language "x" */
	uint32_t ilen;
	unsigned char iself, iparent, icombined, ichildren;
} Pattern;

typedef struct {
	TSQuery *q;
	Pattern *pat;
	unsigned char *face;       /* highlights: per capture id */
	uint32_t content, langcap, filecap;   /* injections: capture ids, or NONE */
	int full;                  /* a combined injection of a language we have */
} Q;

/* Both queries of each language, compiled on first use and kept for the
   life of the editor (Neovim caches queries the same way). */
static struct { Q hl, inj; int loaded; } queries[NLANGS];

typedef struct {
	TSRange *r;                /* n == 0: the whole text */
	uint32_t n;
	TSTree *tree;
	int valid;
} Region;

/* Neovim's LanguageTree: one language, its regions (one tree each), and a
   child per language injected into them, in the order first seen. */
typedef struct LTree {
	int lang;
	struct LTree *parent, **kid;
	Region *reg;
	uint32_t nreg, nkid;
	int scan;                  /* parsed since its injections were found */
	uint32_t pa, pz;           /* bytes whose injections were found */
} LTree;

struct Syntax {
	LTree *root;
	TSParser *p;
	TSQueryCursor *qc;
	Region *susp;              /* the region whose parse a stop suspended */
	int plang;                 /* the parser's language, or -1 */
	int dirty;
	/* Neovim parses injections only where the window shows (the
	   highlighter's parse(range)): the bytes syntax_faces was asked for since
	   the last update, and the lines they cover, which this update serves */
	uint32_t va, vz, ra, rz;
	const Text *t;
	int (*stop)(void);
	char buf[4096];            /* the chunk tree-sitter is reading */
	unsigned char prio[CHUNK]; /* winning priority per byte of a chunk */
	/* The faces of [ca, cz), kept until the trees change: the core asks line
	   by line, and one query over the window costs a third to a tenth of one
	   per line (measured, 48 lines, best of 50: 0.165 vs 0.424 ms in
	   ref/core.c, 0.044 vs 0.620 ms in Neovim's lsp.lua). */
	uint32_t ca, cz;
	unsigned char cache[CHUNK];
};

static char *slurp(const char *path, size_t *n)
{
	FILE *f = fopen(path, "rb");
	char *b = NULL;
	long len;
	if (f && !fseek(f, 0, SEEK_END) && (len = ftell(f)) >= 0 && !fseek(f, 0, SEEK_SET)
	    && (b = malloc((size_t)len + 1)) && fread(b, 1, (size_t)len, f) == (size_t)len)
		*n = (size_t)len;
	else { free(b); b = NULL; }
	if (f) fclose(f);
	return b;
}

static int prio_rank(int p) { return p < 0 ? 0 : p > 254 ? 254 : p; }

#define STR(k, l) ts_query_string_value_for_id(Q->q, st[k].value_id, &(l))
#define ISSTR(k) (st[k].type == TSQueryPredicateStepTypeString)
#define ISCAP(k) (st[k].type == TSQueryPredicateStepTypeCapture)
#define EQ(s, l, lit) ((l) == sizeof lit - 1 && !memcmp(s, lit, l))

/* #set! key [value] or #set! @cap key value, as query.lua's set! */
static void set(Q *Q, Pattern *P, const TSQueryPredicateStep *st, uint32_t a, uint32_t n)
{
	uint32_t l, vl = 0, c = n >= 3 && ISCAP(a + 1), k = a + 1 + c;
	if (n < 2 + c || !ISSTR(k)) return;
	const char *key = STR(k, l), *v = n > 2 + c && ISSTR(k + 1) ? STR(k + 1, vl) : NULL;
	if (EQ(key, l, "priority") && v) {
		if (c) P->pcap = st[a + 1].value_id, P->cprio = atoi(v);
		else P->prio = atoi(v);
	} else if (c) return;
	else if (EQ(key, l, "injection.language") && v) P->ilang = v, P->ilen = vl;
	else if (EQ(key, l, "injection.self")) P->iself = 1;
	else if (EQ(key, l, "injection.parent")) P->iparent = 1;
	else if (EQ(key, l, "injection.combined")) P->icombined = 1;
	else if (EQ(key, l, "injection.include-children")) P->ichildren = 1;
}

/* Turns each pattern's predicate steps into Preds and metadata, as
   query.lua's process_patterns: "not-" negates, "any-" makes a quantified
   capture pass when one node passes.  An unknown predicate fails the query,
   as Neovim errors on it. */
static int compile(Q *Q, int hl)
{
	uint32_t np = ts_query_pattern_count(Q->q), nc = ts_query_capture_count(Q->q);
	Q->pat = calloc(np ? np : 1, sizeof *Q->pat);
	Q->face = calloc(nc ? nc : 1, 1);
	if (!Q->pat || !Q->face) return -1;
	Q->content = Q->langcap = Q->filecap = NONE;
	for (uint32_t c = 0; c < nc; c++) {
		uint32_t l;
		const char *s = ts_query_capture_name_for_id(Q->q, c, &l);
		if (hl) Q->face[c] = s[0] == '_' ? 0 : face_of(s, l);
		else if (EQ(s, l, "injection.content")) Q->content = c;
		else if (EQ(s, l, "injection.language")) Q->langcap = c;
		else if (EQ(s, l, "injection.filename")) Q->filecap = c;
	}
	for (uint32_t i = 0; i < np; i++) {
		uint32_t ns;
		const TSQueryPredicateStep *st = ts_query_predicates_for_pattern(Q->q, i, &ns);
		Pattern *P = &Q->pat[i];
		P->prio = P->cprio = PRIO;
		P->pcap = NONE;
		P->p = calloc(ns ? ns : 1, sizeof *P->p);
		if (!P->p) return -1;
		for (uint32_t a = 0; a < ns; ) {
			uint32_t b = a;
			while (st[b].type != TSQueryPredicateStepTypeDone) b++;
			uint32_t l, n = b - a;
			const char *name = STR(a, l);
			if (EQ(name, l, "set!")) set(Q, P, st, a, n);
			else if (EQ(name, l, "offset!"))
				/* In these languages' queries only injections of languages
				   we lack (YAML, TOML) and a capture with no colour (@_url)
				   use it, so it is left out; a query that needs it loses
				   the pattern, which the check against Neovim shows. */
				ts_query_disable_pattern(Q->q, i);
			else if (name[l - 1] != '!') {
				/* other directives (gsub!, trim!) do not colour or inject */
				Pred *p = &P->p[P->n];
				if (l > 4 && !memcmp(name, "not-", 4)) p->neg = 1, name += 4, l -= 4;
				if (l > 4 && !memcmp(name, "any-", 4)) p->any = 1, name += 4, l -= 4;
				if (EQ(name, l, "eq?")) p->kind = P_EQ;
				else if (EQ(name, l, "lua-match?")) p->kind = P_MATCH;
				else if (EQ(name, l, "contains?")) p->kind = P_CONTAINS;
				else if (EQ(name, l, "any-of?") || EQ(name, l, "of?")) p->kind = P_ANYOF;
				else if (EQ(name, l, "has-ancestor?")) p->kind = P_ANCESTOR;
				else if (EQ(name, l, "has-parent?")) p->kind = P_PARENT;
				else return -1;   /* match? and vim-match? need Vim's regex engine */
				if (n < 2 || !ISCAP(a + 1)) return -1;
				p->cap = st[a + 1].value_id;
				p->cap2 = NONE;
				if (p->kind == P_EQ && n == 3 && ISCAP(a + 2)) p->cap2 = st[a + 2].value_id;
				else {
					p->nstr = n - 2;
					p->str = calloc(p->nstr ? p->nstr : 1, sizeof *p->str);
					p->len = calloc(p->nstr ? p->nstr : 1, sizeof *p->len);
					if (!p->str || !p->len) return -1;
					for (uint32_t k = 0; k < p->nstr; k++) {
						if (!ISSTR(a + 2 + k)) return -1;
						p->str[k] = STR(a + 2 + k, p->len[k]);
					}
				}
				P->n++;
			}
			a = b + 1;
		}
	}
	return 0;
}

typedef struct Src Src;
static int resolve(Src *src, const char *s, uint32_t a, uint32_t z);

static void load1(Q *Q, int li, const char *file, int hl)
{
	char path[512];
	size_t n;
	uint32_t off;
	TSQueryError err;
	snprintf(path, sizeof path, "%s/%s/%s.scm", SYNTAX_QUERIES, langs[li].name, file);
	char *src = slurp(path, &n);
	if (src) Q->q = ts_query_new(langs[li].fn(), src, (uint32_t)n, &off, &err);
	free(src);
	if (Q->q && compile(Q, hl)) {
		ts_query_delete(Q->q);   /* the Preds stay: a few bytes, once */
		Q->q = NULL;
	}
	/* Neovim scans the whole tree when the query has combined injections;
	   one into a language we lack injects nothing, so it needs no scan */
	for (uint32_t i = 0; Q->q && !hl && i < ts_query_pattern_count(Q->q); i++) {
		const Pattern *P = &Q->pat[i];
		Q->full |= P->icombined && (!P->ilang || resolve(NULL, P->ilang, 0, P->ilen) >= 0);
	}
}

static void load(int li)
{
	if (queries[li].loaded) return;
	queries[li].loaded = 1;
	load1(&queries[li].hl, li, "highlights", 1);
	load1(&queries[li].inj, li, "injections", 0);
}

static LTree *ltree(int lang, LTree *parent)
{
	LTree *L = calloc(1, sizeof *L);
	if (!L) return NULL;
	L->lang = lang;
	L->parent = parent;
	load(lang);
	return L;
}

static void free_region(Syntax *s, Region *r)
{
	if (s->susp == r) {
		ts_parser_reset(s->p);
		s->susp = NULL;
	}
	if (r->tree) ts_tree_delete(r->tree);
	free(r->r);
}

static void free_ltree(Syntax *s, LTree *L)
{
	for (uint32_t i = 0; i < L->nkid; i++) free_ltree(s, L->kid[i]);
	for (uint32_t i = 0; i < L->nreg; i++) free_region(s, &L->reg[i]);
	free(L->kid);
	free(L->reg);
	free(L);
}

Syntax *syntax_open(const char *path, const Text *t)
{
	int li = lang_of_path(path, strlen(path));
	if (li < 0) return NULL;
	Syntax *s = calloc(1, sizeof *s);
	if (!s) return NULL;
	s->plang = -1;
	s->dirty = 1;
	s->t = t;
	s->va = UINT32_MAX;
	/* the root is made on the first update, after the first frame: compiling
	   the queries takes 13 to 20 ms (measured) */
	s->root = (LTree *)(intptr_t)(li + 1);
	return s;
}

static int started(const Syntax *s) { return (uintptr_t)s->root > NLANGS; }

void syntax_free(Syntax *s)
{
	if (!s) return;
	if (started(s)) free_ltree(s, s->root);
	if (s->qc) ts_query_cursor_delete(s->qc);
	if (s->p) ts_parser_delete(s->p);
	free(s);
}

static TSPoint point(const Text *t, size_t pos)
{
	size_t r = text_line_of(t, pos);
	return (TSPoint){ (uint32_t)r, (uint32_t)(pos - text_line_start(t, r)) };
}

/* tree-sitter's ts_range_edit, on our copy of a region's ranges */
static TSPoint padd(TSPoint a, TSPoint b) { return b.row ? (TSPoint){ a.row + b.row, b.column } : (TSPoint){ a.row, a.column + b.column }; }
static TSPoint psub(TSPoint a, TSPoint b) { return a.row > b.row ? (TSPoint){ a.row - b.row, a.column } : (TSPoint){ 0, a.column - b.column }; }

static void range_edit(TSRange *r, const TSInputEdit *e)
{
	if (r->end_byte >= e->old_end_byte) {
		if (r->end_byte != UINT32_MAX) {
			r->end_byte = e->new_end_byte + (r->end_byte - e->old_end_byte);
			r->end_point = padd(e->new_end_point, psub(r->end_point, e->old_end_point));
			if (r->end_byte < e->new_end_byte) r->end_byte = UINT32_MAX, r->end_point = (TSPoint){ UINT32_MAX, UINT32_MAX };
		}
	} else if (r->end_byte > e->start_byte) {
		r->end_byte = e->start_byte;
		r->end_point = e->start_point;
	}
	if (r->start_byte >= e->old_end_byte) {
		r->start_byte = e->new_end_byte + (r->start_byte - e->old_end_byte);
		r->start_point = padd(e->new_end_point, psub(r->start_point, e->old_end_point));
		if (r->start_byte < e->new_end_byte) r->start_byte = UINT32_MAX, r->start_point = (TSPoint){ UINT32_MAX, UINT32_MAX };
	} else if (r->start_byte > e->start_byte) {
		r->start_byte = e->start_byte;
		r->start_point = e->start_point;
	}
}

/* LanguageTree:_edit: edit every tree and region; a region meeting the edit
   (or the whole text) must be parsed again */
static void edit_ltree(LTree *L, const TSInputEdit *e)
{
	for (uint32_t i = 0; i < L->nreg; i++) {
		Region *g = &L->reg[i];
		if (g->tree) ts_tree_edit(g->tree, e);
		int hit = !g->n;
		for (uint32_t k = 0; k < g->n; k++) {
			range_edit(&g->r[k], e);
			hit |= g->r[k].end_byte > e->start_byte && g->r[k].start_byte < e->old_end_byte;
		}
		if (hit) g->valid = 0;
	}
	for (uint32_t i = 0; i < L->nkid; i++) edit_ltree(L->kid[i], e);
}

void syntax_edit(Syntax *s, const Text *t, size_t pos, size_t ndel, const char *ins, size_t nins)
{
	if (!s) return;
	s->dirty = 1;
	s->t = t;
	s->cz = 0;
	if (s->susp) {
		/* the text a suspended parse was reading has changed: start it again */
		ts_parser_reset(s->p);
		s->susp = NULL;
	}
	if (!started(s)) return;
	TSInputEdit e;
	e.start_byte = (uint32_t)pos;
	e.old_end_byte = (uint32_t)(pos + ndel);
	e.new_end_byte = (uint32_t)(pos + nins);
	e.start_point = point(t, pos);
	e.old_end_point = point(t, pos + ndel);
	e.new_end_point = e.start_point;
	for (size_t i = 0; i < nins; i++)
		if (ins[i] == '\n') e.new_end_point.row++, e.new_end_point.column = 0;
		else e.new_end_point.column++;
	edit_ltree(s->root, &e);
}

/* Bytes of the text through a small window, so a predicate can read a node
   of any size without allocating in the redraw path. */
struct Src { const Text *t; size_t base, n; char w[256]; };

static int at(void *v, size_t i)
{
	Src *s = v;
	if (i < s->base || i >= s->base + s->n) {
		s->base = i;
		s->n = text_read(s->t, i, s->w, sizeof s->w);
		if (!s->n) return -1;
	}
	return (unsigned char)s->w[i - s->base];
}

static int text_eq(Src *src, uint32_t a, uint32_t z, const char *str, uint32_t len)
{
	if (z - a != len) return 0;
	for (uint32_t i = 0; i < len; i++)
		if (at(src, a + i) != (unsigned char)str[i]) return 0;
	return 1;
}

static int has_type(TSNode n, const Pred *p)
{
	const char *ty = ts_node_type(n);
	size_t l = strlen(ty);
	for (uint32_t k = 0; k < p->nstr; k++)
		if (p->len[k] == l && !memcmp(p->str[k], ty, l)) return 1;
	return 0;
}

/* One predicate on one node, as query.lua's handlers. */
static int test1(Src *src, const TSQueryMatch *m, const Pred *p, TSNode n)
{
	uint32_t a = ts_node_start_byte(n), z = ts_node_end_byte(n);
	switch (p->kind) {
	case P_EQ:
		if (p->cap2 != NONE) {
			for (uint16_t i = 0; i < m->capture_count; i++)
				if (m->captures[i].index == p->cap2) {
					TSNode o = m->captures[i].node;
					uint32_t oa = ts_node_start_byte(o), oz = ts_node_end_byte(o);
					if (oz - oa != z - a) return 0;
					for (uint32_t k = 0; k < z - a; k++) {
						int c = at(src, oa + k);
						if (at(src, a + k) != c) return 0;
					}
					return 1;
				}
			return 0;
		}
		return p->nstr && text_eq(src, a, z, p->str[0], p->len[0]);
	case P_MATCH:
		return p->nstr && lmatch_find(at, src, a, z, p->str[0], p->len[0]);
	case P_CONTAINS:
		/* each string, with the predicate's any or all (query.lua's contains) */
		for (uint32_t k = 0; k < p->nstr; k++) {
			int found = 0;
			for (uint32_t i = a; !found && i + p->len[k] <= z; i++) {
				uint32_t j = 0;
				while (j < p->len[k] && at(src, i + j) == (unsigned char)p->str[k][j]) j++;
				found = j == p->len[k];
			}
			if (found == p->any) return found;
		}
		return !p->any;
	case P_ANYOF:
		for (uint32_t k = 0; k < p->nstr; k++)
			if (text_eq(src, a, z, p->str[k], p->len[k])) return 1;
		return 0;
	case P_ANCESTOR:
		/* down from the root, as Neovim's __has_ancestor: each ts_node_parent
		   walks from the root again (measured: a fifth of the colouring time) */
		for (TSNode u = ts_tree_root_node(n.tree); !ts_node_is_null(u) && u.id != n.id;
		     u = ts_node_child_with_descendant(u, n))
			if (has_type(u, p)) return 1;
		return 0;
	default: { /* P_PARENT */
		TSNode u = ts_node_parent(n);
		return !ts_node_is_null(u) && has_type(u, p);
	}
	}
}

/* A predicate over every node of its capture: none passes vacuously; "all"
   unless any-; any-of?, has-ancestor? and has-parent? are "any" always. */
static int test(Src *src, const TSQueryMatch *m, const Pred *p)
{
	int any = p->any || p->kind >= P_ANYOF, seen = 0;
	for (uint16_t i = 0; i < m->capture_count; i++) {
		if (m->captures[i].index != p->cap) continue;
		seen = 1;
		int r = test1(src, m, p, m->captures[i].node);
		if (any && r) return 1;
		if (!any && !r) return 0;
	}
	return !seen || !any;
}

static int passes(Src *src, const TSQueryMatch *m, const Pattern *P)
{
	for (uint32_t i = 0; i < P->n; i++)
		if (test(src, m, &P->p[i]) == P->p[i].neg) return 0;
	return 1;
}

/* ---- parsing (deferred work) ---- */

static const char *input(void *payload, uint32_t at, TSPoint pt, uint32_t *n)
{
	(void)pt;
	Syntax *s = payload;
	*n = (uint32_t)text_read(s->t, at, s->buf, sizeof s->buf);
	return s->buf;
}

static bool progress(TSParseState *st)
{
	Syntax *s = st->payload;
	return s->stop && s->stop();
}

typedef struct { TSRange *r; uint32_t n, cap; } Ranges;

static int push(Ranges *v, TSRange r)
{
	if (v->n == v->cap) {
		uint32_t c = v->cap ? 2 * v->cap : 4;
		TSRange *b = realloc(v->r, c * sizeof *b);
		if (!b) return -1;
		v->r = b;
		v->cap = c;
	}
	v->r[v->n++] = r;
	return 0;
}

/* Neovim's resolve_lang: a language name, spaces dropped, any case, - as _ */
static int resolve(Src *src, const char *s, uint32_t a, uint32_t z)
{
	char b[32];
	uint32_t n = 0;
	for (uint32_t i = a; i < z; i++) {
		int c = s ? (unsigned char)s[i] : at(src, i);
		if (isspace(c)) continue;
		if (n == sizeof b - 1) return -1;
		c = c == '-' ? '_' : tolower(c);
		if (!isalnum(c) && c != '_') return -1;
		b[n++] = (char)c;
	}
	for (int i = 0; i < NLANGS; i++)
		if (strlen(langs[i].name) == n && !memcmp(langs[i].name, b, n)) return i;
	return -1;
}

static TSRange mkrange(uint32_t a, TSPoint pa, uint32_t z, TSPoint pz)
{
	return (TSRange){ pa, pz, a, z };
}

/* get_node_ranges: the node's range without its named children, unless
   injection.include-children */
static int node_ranges(TSNode n, const Pattern *P, Ranges *out)
{
	TSPoint sp = ts_node_start_point(n), ep = ts_node_end_point(n);
	uint32_t sb = ts_node_start_byte(n), eb = ts_node_end_byte(n);
	uint32_t nc = ts_node_named_child_count(n);
	if (P->ichildren || !nc) return push(out, mkrange(sb, sp, eb, ep));
	for (uint32_t i = 0; i < nc; i++) {
		TSNode c = ts_node_named_child(n, i);
		TSPoint cs = ts_node_start_point(c);
		if (cs.row > sp.row || cs.column > sp.column)
			if (push(out, mkrange(sb, sp, ts_node_start_byte(c), cs))) return -1;
		sp = ts_node_end_point(c);
		sb = ts_node_end_byte(c);
	}
	if (ep.row > sp.row || ep.column > sp.column) return push(out, mkrange(sb, sp, eb, ep));
	return 0;
}

/* clip_regions: the parts of a inside the parent region's ranges p */
static int clip(const Ranges *a, const Region *p, Ranges *out)
{
	if (!p->n) {
		for (uint32_t i = 0; i < a->n; i++)
			if (push(out, a->r[i])) return -1;
		return 0;
	}
	for (uint32_t i = 0, j = 0; i < a->n && j < p->n; ) {
		const TSRange *x = &a->r[i], *y = &p->r[j];
		if (x->end_byte > y->start_byte && x->start_byte < y->end_byte) {
			TSRange r = x->start_byte >= y->start_byte ? *x : *y;
			const TSRange *e = x->end_byte <= y->end_byte ? x : y;
			r.end_byte = e->end_byte;
			r.end_point = e->end_point;
			if (push(out, r)) return -1;
		}
		if (x->end_byte <= y->end_byte) i++;
		else j++;
	}
	return 0;
}

/* One injected region found by the scan of a tree. */
typedef struct { int lang; uint32_t pattern; Ranges r; } Inj;
typedef struct { Inj *v; uint32_t n, cap; } Injs;

static Inj *inj_new(Injs *I, int lang, uint32_t pattern)
{
	if (I->n == I->cap) {
		uint32_t c = I->cap ? 2 * I->cap : 8;
		Inj *b = realloc(I->v, c * sizeof *b);
		if (!b) return NULL;
		I->v = b;
		I->cap = c;
	}
	Inj *j = &I->v[I->n++];
	*j = (Inj){ lang, pattern, { NULL, 0, 0 } };
	return j;
}

static int same(const TSRange *a, uint32_t na, const TSRange *b, uint32_t nb)
{
	if (na != nb) return 0;
	for (uint32_t i = 0; i < na; i++)
		if (a[i].start_byte != b[i].start_byte || a[i].end_byte != b[i].end_byte
		    || a[i].start_point.row != b[i].start_point.row || a[i].start_point.column != b[i].start_point.column
		    || a[i].end_point.row != b[i].end_point.row || a[i].end_point.column != b[i].end_point.column)
			return 0;
	return 1;
}

/* LanguageTree:set_included_regions for the child of one language */
static int set_regions(Syntax *s, LTree *K, Injs *I, int lang)
{
	uint32_t n = 0;
	for (uint32_t i = 0; i < I->n; i++) n += I->v[i].lang == lang;
	if (n != K->nreg) {
		for (uint32_t i = 0; i < K->nreg; i++) free_region(s, &K->reg[i]);
		free(K->reg);
		K->reg = calloc(n, sizeof *K->reg);
		K->nreg = 0;
		if (!K->reg) return -1;
		for (uint32_t i = 0; i < I->n; i++)
			if (I->v[i].lang == lang) {
				K->reg[K->nreg++].r = I->v[i].r.r;
				K->reg[K->nreg - 1].n = I->v[i].r.n;
				I->v[i].r.r = NULL;
			}
		return 0;
	}
	for (uint32_t i = 0, k = 0; i < I->n; i++) {
		if (I->v[i].lang != lang) continue;
		Region *g = &K->reg[k++];
		if (!same(g->r, g->n, I->v[i].r.r, I->v[i].r.n)) {
			if (s->susp == g) {
				/* its stopped parse reads the old ranges: start it again */
				ts_parser_reset(s->p);
				s->susp = NULL;
			}
			g->valid = 0;
			free(g->r);
			g->r = I->v[i].r.r;
			g->n = I->v[i].r.n;
			I->v[i].r.r = NULL;
		}
	}
	return 0;
}

/* LanguageTree:_get_injections and _add_injections, over every tree of L */
static int inject(Syntax *s, LTree *L, const Text *t)
{
	uint32_t a = s->ra, z = s->rz;
	Q *Q = &queries[L->lang].inj;
	Injs I = { NULL, 0, 0 };
	Ranges cr = { NULL, 0, 0 };
	Src src = { t, 0, 0, { 0 } };
	int rc = 0;
	for (uint32_t ti = 0; Q->q && ti < L->nreg && !rc; ti++) {
		if (!L->reg[ti].tree) continue;
		uint32_t first = I.n;   /* combined injections are per tree */
		ts_query_cursor_set_byte_range(s->qc, Q->full ? 0 : a, Q->full ? UINT32_MAX : z);
		ts_query_cursor_exec(s->qc, Q->q, ts_tree_root_node(L->reg[ti].tree));
		TSQueryMatch m;
		while (!rc && ts_query_cursor_next_match(s->qc, &m)) {
			const Pattern *P = &Q->pat[m.pattern_index];
			if (!passes(&src, &m, P)) continue;
			int lang = P->iself ? L->lang : P->iparent ? (L->parent ? L->parent->lang : -1)
			         : P->ilang ? resolve(&src, P->ilang, 0, P->ilen) : -1;
			cr.n = 0;
			for (uint16_t i = 0; i < m.capture_count && !rc; i++) {
				const TSQueryCapture *c = &m.captures[i];
				uint32_t a = ts_node_start_byte(c->node), z = ts_node_end_byte(c->node);
				if (c->index == Q->langcap) lang = resolve(&src, NULL, a, z);
				else if (c->index == Q->filecap) {
					/* vim.filetype.match by name: here, by the ending */
					char b[256];
					uint32_t k = 0;
					for (uint32_t j = a; j < z && k < sizeof b; j++) b[k++] = (char)at(&src, j);
					lang = lang_of_path(b, k);
				} else if (c->index == Q->content) rc = node_ranges(c->node, P, &cr);
			}
			if (rc || lang < 0 || !cr.n) continue;
			Inj *j = NULL;
			if (P->icombined)
				for (uint32_t k = first; k < I.n && !j; k++)
					if (I.v[k].lang == lang && I.v[k].pattern == m.pattern_index) j = &I.v[k];
			if (!j && !(j = inj_new(&I, lang, P->icombined ? m.pattern_index : NONE))) rc = -1;
			else rc = clip(&cr, &L->reg[ti], &j->r);
		}
	}
	/* children in the order their languages first appear; unused ones go */
	uint32_t keep = 0;
	for (uint32_t i = 0; i < I.n && !rc; i++) {
		int lang = I.v[i].lang, seen = 0;
		for (uint32_t k = 0; k < i; k++) seen |= I.v[k].lang == lang;
		if (seen) continue;
		uint32_t k = keep;
		while (k < L->nkid && L->kid[k]->lang != lang) k++;
		if (k == L->nkid) {
			LTree **b = realloc(L->kid, (L->nkid + 1) * sizeof *b), *K;
			if (!b) { rc = -1; break; }
			L->kid = b;
			if (!(K = ltree(lang, L))) { rc = -1; break; }
			L->kid[L->nkid++] = K;
		}
		LTree *K = L->kid[k];
		memmove(&L->kid[keep + 1], &L->kid[keep], (k - keep) * sizeof *L->kid);
		L->kid[keep++] = K;
		rc = set_regions(s, K, &I, lang);
	}
	if (!rc) {
		for (uint32_t k = keep; k < L->nkid; k++) free_ltree(s, L->kid[k]);
		L->nkid = keep;
		L->pa = Q->full ? 0 : a;
		L->pz = Q->full ? UINT32_MAX : z;
	}
	for (uint32_t i = 0; i < I.n; i++) free(I.v[i].r.r);
	free(I.v);
	free(cr.r);
	return rc;
}

/* LanguageTree:_parse: parse the invalid regions, find the injections, then
   the children.  2 if stopped (call again), 1 if a tree changed, 0 if not,
   -1 out of memory. */
static int parse(Syntax *s, LTree *L, const Text *t)
{
	int changed = 0;
	for (uint32_t i = 0; i < L->nreg; i++) {
		Region *g = &L->reg[i];
		if (g->valid || (g->n && (g->r[0].start_byte >= s->rz || g->r[g->n - 1].end_byte <= s->ra)))
			continue;
		/* many small regions each parse in under the 100 steps between
		   progress checks, so ask between regions too */
		if (s->susp != g && s->stop && s->stop()) return 2;
		if (s->susp && s->susp != g) {
			/* the suspended region left the window: drop its parse */
			ts_parser_reset(s->p);
			s->susp = NULL;
		}
		if (s->susp != g) {
			if (s->plang != L->lang) {
				if (!ts_parser_set_language(s->p, langs[L->lang].fn())) return -1;
				s->plang = L->lang;
			}
			if (!ts_parser_set_included_ranges(s->p, g->r, g->n)) return -1;
		}
		TSInput in = { s, input, TSInputEncodingUTF8, NULL };
		TSTree *n = ts_parser_parse_with_options(s->p, g->tree, in, (TSParseOptions){ s, progress });
		if (!n) {
			s->susp = g;
			return 2;
		}
		s->susp = NULL;
		if (g->tree) ts_tree_delete(g->tree);
		g->tree = n;
		g->valid = 1;
		L->scan = changed = 1;
	}
	if (L->scan || s->ra < L->pa || s->rz > L->pz) {
		if (inject(s, L, t)) return -1;
		L->scan = 0;
	}
	for (uint32_t i = 0; i < L->nkid; i++) {
		int r = parse(s, L->kid[i], t);
		if (r == 2 || r < 0) return r;
		changed |= r;
	}
	return changed;
}

int syntax_update(Syntax *s, const Text *t, int (*stop)(void))
{
	if (!s || !s->dirty) return 0;
	if (!started(s)) {
		int li = (int)(intptr_t)s->root - 1;
		LTree *R = ltree(li, NULL);
		s->p = ts_parser_new();
		s->qc = ts_query_cursor_new();
		if (!R || !s->p || !s->qc || !(R->reg = calloc(1, sizeof *R->reg))) {
			if (R) free(R);
			s->dirty = 0;      /* no colours for this buffer */
			return 0;
		}
		R->nreg = 1;
		s->root = R;
		ts_query_cursor_set_match_limit(s->qc, LIMIT);
	}
	s->t = t;
	s->stop = stop;
	s->cz = 0;
	if (s->va < s->vz) {
		/* whole lines, as iter_matches(root, source, start_row, end_row + 1) */
		s->ra = (uint32_t)text_line_start(t, text_line_of(t, s->va));
		s->rz = (uint32_t)text_line_start(t, text_line_of(t, s->vz - 1) + 1);
		s->va = UINT32_MAX;
		s->vz = 0;
	}
	int r = parse(s, s->root, t);
	if (r == 2) return 2;
	s->dirty = 0;
	return r == 1;
}

/* ---- colours (the redraw path: no allocation) ---- */

static void faces_tree(Syntax *s, const Text *t, LTree *L, uint32_t a, uint32_t z, unsigned char *face)
{
	Q *Q = &queries[L->lang].hl;
	for (uint32_t ri = 0; Q->q && ri < L->nreg; ri++) {
		Region *g = &L->reg[ri];
		if (!g->tree) continue;
		if (g->n && (g->r[0].start_byte >= z || g->r[g->n - 1].end_byte <= a)) continue;
		ts_query_cursor_set_byte_range(s->qc, a, z);
		ts_query_cursor_exec(s->qc, Q->q, ts_tree_root_node(g->tree));
		Src src = { t, 0, 0, { 0 } };
		TSQueryMatch m;
		uint32_t ci;
		while (ts_query_cursor_next_capture(s->qc, &m, &ci)) {
			const Pattern *P = &Q->pat[m.pattern_index];
			if (!passes(&src, &m, P)) {
				ts_query_cursor_remove_match(s->qc, m.id);
				continue;
			}
			const TSQueryCapture *c = &m.captures[ci];
			unsigned char f = Q->face[c->index];
			if (!f) continue;
			int pr = prio_rank(c->index == P->pcap ? P->cprio : P->prio) + 1;
			uint32_t b = ts_node_start_byte(c->node), e = ts_node_end_byte(c->node);
			/* the highlighter colours only inside the tree's included ranges */
			for (uint32_t k = 0; k < (g->n ? g->n : 1); k++) {
				uint32_t rb = b, re = e;
				if (g->n) {
					if (rb < g->r[k].start_byte) rb = g->r[k].start_byte;
					if (re > g->r[k].end_byte) re = g->r[k].end_byte;
				}
				if (rb < a) rb = a;
				if (re > z) re = z;
				for (uint32_t i = rb; i < re; i++)
					if (pr >= s->prio[i - a]) s->prio[i - a] = (unsigned char)pr, face[i - a] = f;
			}
		}
	}
	for (uint32_t i = 0; i < L->nkid; i++) faces_tree(s, t, L->kid[i], a, z, face);
}

void syntax_faces(Syntax *s, size_t a, size_t z, unsigned char *face)
{
	memset(face, 0, z - a);
	if (!s || a >= z) return;
	if (a < s->va) s->va = (uint32_t)a;
	if (z > s->vz) s->vz = (uint32_t)z;
	/* bytes whose injections are not known yet: the next update finds them */
	if (a < s->ra || z > s->rz) s->dirty = 1;
	if (!started(s)) return;
	if (a >= s->ca && z <= s->cz) {
		memcpy(face, s->cache + (a - s->ca), z - a);
		return;
	}
	if (z - a < CHUNK) {
		/* a window as large as the last one shown, from here */
		size_t w = s->rz - s->ra > z - a ? s->rz - s->ra : z - a, n = text_len(s->t);
		s->ca = (uint32_t)a;
		s->cz = (uint32_t)(w > CHUNK ? a + CHUNK : a + w > n ? n : a + w);
		if (s->cz < z) s->cz = (uint32_t)z;
		memset(s->cache, 0, s->cz - s->ca);
		memset(s->prio, 0, s->cz - s->ca);
		faces_tree(s, s->t, s->root, s->ca, s->cz, s->cache);
		memcpy(face, s->cache, z - a);
		return;
	}
	for (size_t c = a; c < z; c += CHUNK) {
		uint32_t ca = (uint32_t)c, cz = (uint32_t)(z - c < CHUNK ? z : c + CHUNK);
		memset(s->prio, 0, cz - ca);
		faces_tree(s, s->t, s->root, ca, cz, face + (c - a));
	}
}
