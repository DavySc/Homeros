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
#include <stdlib.h>
#include <string.h>
#include <tree_sitter/api.h>
#include "grid.h"
#include "syntax.h"
#include "lmatch.h"
#include "tsql.h"

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
enum { NNAMES = sizeof names / sizeof *names, PRIO = 100, CHUNK = 8192, WINDOW = 65536, LIMIT = 256 };
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

/* Grammars linked in, by Neovim's name for each */
const TSLanguage *tree_sitter_c(void);
const TSLanguage *tree_sitter_lua(void);
const TSLanguage *tree_sitter_markdown(void);
const TSLanguage *tree_sitter_markdown_inline(void);
const TSLanguage *tree_sitter_rust(void);
static const struct Lang {
	const char *name;
	const TSLanguage *(*fn)(void);
} langs[] = {
	{ "c", tree_sitter_c },
	{ "lua", tree_sitter_lua },
	{ "markdown", tree_sitter_markdown },
	{ "markdown_inline", tree_sitter_markdown_inline },
	{ "rust", tree_sitter_rust },
};
enum { NLANGS = sizeof langs / sizeof *langs };

/* File name endings, or whole names, Neovim's 'filetype' for each (nvim
   0.12 --clean, vim.filetype.match: a .h is cpp unless g:c_syntax_for_h;
   a .v is Verilog or V by its content, so it is left out; Neovim also
   strips backup endings such as ~ and .bak, which these do not; it knows no
   .dl, which is souffle as the Soufflé plugins for Vim name it), and what
   colours it: a langs[] index, T-SQL's, JSON's or Soufflé's word lists
   (tsql.c, in tsql_new's order), or nothing yet (Davy's other languages,
   FEATURES.md) */
enum { BY_NONE = -1, BY_SQL = -2, BY_JSON = -3, BY_DL = -4 };
static const struct { const char *ext, *ft; int lang; } fts[] = {
	{ ".c", "c", 0 }, { ".h", "cpp", 0 }, { ".lua", "lua", 1 }, { ".md", "markdown", 2 },
	{ ".sql", "sql", BY_SQL }, { ".json", "json", BY_JSON }, { "CMakeLists.txt", "cmake", BY_NONE },
	{ ".txt", "text", BY_NONE }, { ".rs", "rust", 4 }, { ".nix", "nix", BY_NONE },
	{ ".glsl", "glsl", BY_NONE }, { ".vert", "glsl", BY_NONE }, { ".frag", "glsl", BY_NONE },
	{ ".comp", "glsl", BY_NONE }, { ".geom", "glsl", BY_NONE }, { ".tesc", "glsl", BY_NONE },
	{ ".tese", "glsl", BY_NONE }, { ".sv", "systemverilog", BY_NONE }, { ".svh", "systemverilog", BY_NONE },
	{ ".cmake", "cmake", BY_NONE }, { ".lean", "lean", BY_NONE }, { ".cs", "cs", BY_NONE },
	{ ".org", "org", BY_NONE }, { ".dl", "souffle", BY_DL },
};

/* fts[]'s entry for path's base name (after its last / or \, as Neovim),
   -1 for none */
static int ft_of(const char *path)
{
	const char *b = path;
	for (const char *c = path; *c; c++)
		if (*c == '/' || *c == '\\') b = c + 1;
	size_t bl = strlen(b);
	for (int i = 0; i < (int)(sizeof fts / sizeof *fts); i++) {
		const char *e = fts[i].ext;
		size_t el = strlen(e);
		if (bl >= el && !memcmp(b + bl - el, e, el) && (*e == '.' || bl == el)) return i;
	}
	return -1;
}

const char *syntax_filetype(const char *path)
{
	int i = path ? ft_of(path) : -1;
	return i < 0 ? NULL : fts[i].ft;
}

/* Neovim's highlights and injections queries per language, in langs' order
   (build.mk compiles them in) */
extern const char *const syntax_queries[][2];
int syntax_release(TSTree *t, unsigned n);   /* release.c */
enum { RELEASE_STEP = 1024 };                /* nodes freed between stop asks */

enum { P_EQ, P_MATCH, P_ANYOF, P_ANCESTOR, P_PARENT, P_CONTAINS };
typedef struct {
	unsigned char kind, neg;
	uint32_t cap;
	uint32_t nstr;
	const char **str;          /* the query owns the strings */
	uint32_t *len;
} Pred;
/* A pattern's predicates and the metadata its directives set. */
typedef struct {
	Pred *p;
	uint32_t n;
	int prio;                  /* #set! priority N */
	const char *ilang;         /* #set! injection.language "x" */
	uint32_t ilen;
	unsigned char iself, incl;  /* injection.self, injection.include-children */
} Pattern;

typedef struct {
	TSQuery *q;
	Pattern *pat;
	unsigned char *face;       /* highlights: per capture id */
	uint32_t content, langcap; /* injections: capture ids, or NONE */
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
	struct LTree **kid;
	Region *reg;
	uint32_t nreg, nkid;
	int scan;                  /* parsed since its injections were found */
	uint32_t pa, pz;           /* bytes whose injections were found */
} LTree;

struct Syntax {
	Tsql *sql;                 /* T-SQL by word lists, or JSON, instead (tsql.c) */
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
	/* The faces of [ca, cz), kept until the trees change: the core asks line
	   by line, and one query over the window costs a third to a tenth of one
	   per line (measured, 48 lines, best of 50: 0.165 vs 0.424 ms in
	   ref/core.c, 0.044 vs 0.620 ms in Neovim's lsp.lua).  It holds the
	   whole window, filled by one query from its first line, as the
	   highlighter's one iter_captures per redraw from the first row drawn:
	   where captures of one priority overlap, tree-sitter's order between
	   them depends on where the query starts (measured: fills from a line
	   start differ from one from the top in 8 of 36,759 edited texts).
	   cap bytes each for the faces and the winning priorities; grown by
	   syntax_update, to the window it serves, never in the redraw.  A
	   window past WINDOW bytes (lines of tens of kilobytes) is filled in
	   CHUNK pieces from each line asked instead, where that order may
	   differ: with a 1.1 MB line in the window, one query costs up to
	   770 ms a frame after a key (measured, worst of 5 runs; 15 ms in
	   pieces). */
	uint32_t ca, cz, cap;
	unsigned char *cache, *prio;
	/* Bytes edited since the cache was filled (sa < sz): until the next
	   parse the trees are only edited, so a face can change only under a
	   match that has a capture over these bytes, and syntax_faces
	   recomputes just those (the frame after a key), as Emacs's jit-lock
	   refontifies only the changed region widened to whole constructs
	   (jit-lock-after-change, font-lock-extend-region-functions).  A new
	   tree empties the cache instead: tree-sitter's changed ranges miss
	   changes that error recovery makes elsewhere (measured: a newline in
	   a C string turned a later "char" from primitive_type into
	   identifier, with only the newline's bytes reported). */
	uint32_t sa, sz;
};

static int prio_rank(int p) { return p < 0 ? 0 : p > 254 ? 254 : p; }

/* where byte x of the text before an edit of d bytes at p into i is now */
static uint32_t moved(uint32_t x, uint32_t p, uint32_t d, uint32_t i)
{
	return x <= p ? x : x >= p + d ? x - d + i : p;
}

#define STR(k, l) ts_query_string_value_for_id(Q->q, st[k].value_id, &(l))
#define ISSTR(k) (st[k].type == TSQueryPredicateStepTypeString)
#define ISCAP(k) (st[k].type == TSQueryPredicateStepTypeCapture)
#define EQ(s, l, lit) ((l) == sizeof lit - 1 && !memcmp(s, lit, l))

/* #set! key [value], as query.lua's set!.  Of its keys, these languages'
   queries use only these (grep); injection.combined and include-children
   appear only in injections of languages we lack (HTML, LaTeX, YAML, TOML),
   which inject nothing, so those patterns are disabled.  #set! @capture
   key value sets nothing that colours or injects here. */
static void set(Q *Q, uint32_t i, Pattern *P, const TSQueryPredicateStep *st, uint32_t a, uint32_t n)
{
	uint32_t l, vl = 0;
	if (n < 2 || !ISSTR(a + 1)) return;
	const char *key = STR(a + 1, l), *v = n > 2 && ISSTR(a + 2) ? STR(a + 2, vl) : NULL;
	if (EQ(key, l, "priority") && v) P->prio = atoi(v);
	else if (EQ(key, l, "injection.language") && v) P->ilang = v, P->ilen = vl;
	else if (EQ(key, l, "injection.self")) P->iself = 1;
	else if (EQ(key, l, "injection.include-children")) P->incl = 1;
	else if (EQ(key, l, "injection.combined")) ts_query_disable_pattern(Q->q, i);
}

typedef struct Src Src;
static int resolve(Src *src, const char *s, uint32_t a, uint32_t z);

/* An injection of a language we lack, named in the pattern: like Neovim
   without that parser, it injects nothing, so the pattern goes before its
   predicates are read (Rust's re2c pattern uses Vim's match?). */
static int lacked(const TSQueryPredicateStep *st, uint32_t ns, const TSQuery *q)
{
	for (uint32_t a = 0; a + 2 < ns; a++) {
		uint32_t l, vl;
		if (st[a].type != TSQueryPredicateStepTypeString || st[a + 1].type != TSQueryPredicateStepTypeString
		    || st[a + 2].type != TSQueryPredicateStepTypeString) continue;
		const char *n = ts_query_string_value_for_id(q, st[a].value_id, &l);
		const char *k = ts_query_string_value_for_id(q, st[a + 1].value_id, &vl);
		if (!EQ(n, l, "set!") || !EQ(k, vl, "injection.language")) continue;
		k = ts_query_string_value_for_id(q, st[a + 2].value_id, &vl);
		return resolve(NULL, k, 0, vl) < 0;
	}
	return 0;
}

/* Turns each pattern's predicate steps into Preds and metadata, as
   query.lua's process_patterns: "not-" negates.  A predicate these
   languages' queries do not use (match?, the any- forms, #eq? of two
   captures) fails the query, as Neovim errors on an unknown one. */
static int compile(Q *Q, int hl)
{
	uint32_t np = ts_query_pattern_count(Q->q), nc = ts_query_capture_count(Q->q);
	Q->pat = calloc(np ? np : 1, sizeof *Q->pat);
	Q->face = calloc(nc ? nc : 1, 1);
	if (!Q->pat || !Q->face) return -1;
	Q->content = Q->langcap = NONE;
	for (uint32_t c = 0; c < nc; c++) {
		uint32_t l;
		const char *s = ts_query_capture_name_for_id(Q->q, c, &l);
		if (hl) Q->face[c] = s[0] == '_' ? 0 : face_of(s, l);
		else if (EQ(s, l, "injection.content")) Q->content = c;
		else if (EQ(s, l, "injection.language")) Q->langcap = c;
	}
	for (uint32_t i = 0; i < np; i++) {
		uint32_t ns;
		const TSQueryPredicateStep *st = ts_query_predicates_for_pattern(Q->q, i, &ns);
		Pattern *P = &Q->pat[i];
		P->prio = PRIO;
		P->p = calloc(ns ? ns : 1, sizeof *P->p);
		if (!P->p) return -1;
		if (!hl && lacked(st, ns, Q->q)) {
			ts_query_disable_pattern(Q->q, i);
			continue;
		}
		for (uint32_t a = 0; a < ns; ) {
			uint32_t b = a;
			while (st[b].type != TSQueryPredicateStepTypeDone) b++;
			uint32_t l, n = b - a;
			const char *name = STR(a, l);
			if (EQ(name, l, "set!")) set(Q, i, P, st, a, n);
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
				if (EQ(name, l, "eq?")) p->kind = P_EQ;
				else if (EQ(name, l, "lua-match?")) p->kind = P_MATCH;
				else if (EQ(name, l, "any-of?")) p->kind = P_ANYOF;
				else if (EQ(name, l, "has-ancestor?")) p->kind = P_ANCESTOR;
				else if (EQ(name, l, "has-parent?")) p->kind = P_PARENT;
				else if (EQ(name, l, "contains?")) p->kind = P_CONTAINS;
				else return -1;
				if (n < 2 || !ISCAP(a + 1)) return -1;
				p->cap = st[a + 1].value_id;
				p->nstr = n - 2;
				p->str = calloc(p->nstr ? p->nstr : 1, sizeof *p->str);
				p->len = calloc(p->nstr ? p->nstr : 1, sizeof *p->len);
				if (!p->str || !p->len) return -1;
				for (uint32_t k = 0; k < p->nstr; k++) {
					if (!ISSTR(a + 2 + k)) return -1;
					p->str[k] = STR(a + 2 + k, p->len[k]);
				}
				P->n++;
			}
			a = b + 1;
		}
	}
	return 0;
}

static void load1(Q *Q, int li, int hl)
{
	uint32_t off;
	TSQueryError err;
	const char *src = syntax_queries[li][!hl];
	Q->q = ts_query_new(langs[li].fn(), src, (uint32_t)strlen(src), &off, &err);
	if (Q->q && compile(Q, hl)) {
		ts_query_delete(Q->q);   /* the Preds stay: a few bytes, once */
		Q->q = NULL;
	}
}

static void load(int li)
{
	if (queries[li].loaded) return;
	queries[li].loaded = 1;
	load1(&queries[li].hl, li, 1);
	load1(&queries[li].inj, li, 0);
}

static LTree *ltree(int lang)
{
	LTree *L = calloc(1, sizeof *L);
	if (!L) return NULL;
	L->lang = lang;
	load(lang);
	return L;
}

static void free_region(Syntax *s, Region *r)
{
	if (s->susp == r) {
		ts_parser_reset(s->p);
		s->susp = NULL;
	}
	if (r->tree) s->cz = 0, syntax_release(r->tree, 0);
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
	int i = ft_of(path), li = i < 0 ? BY_NONE : fts[i].lang;
	if (li == BY_NONE) return NULL;
	if (li < BY_NONE) {
		Syntax *s = calloc(1, sizeof *s);
		if (s && !(s->sql = tsql_new(t, BY_SQL - li))) free(s), s = NULL;
		return s;
	}
	Syntax *s = calloc(1, sizeof *s);
	if (!s) return NULL;
	s->plang = -1;
	s->dirty = 1;
	s->t = t;
	s->va = UINT32_MAX;
	s->cap = CHUNK;            /* syntax_faces' largest request */
	s->cache = malloc(2 * CHUNK);
	if (!s->cache) return free(s), NULL;
	s->prio = s->cache + CHUNK;
	/* the root is made on the first update, after the first frame: compiling
	   the queries takes 13 to 20 ms (measured) */
	s->root = (LTree *)(intptr_t)(li + 1);
	return s;
}

static int started(const Syntax *s) { return (uintptr_t)s->root > NLANGS; }

void syntax_free(Syntax *s)
{
	if (!s) return;
	tsql_free(s->sql);
	if (started(s)) free_ltree(s, s->root);
	if (s->qc) ts_query_cursor_delete(s->qc);
	if (s->p) ts_parser_delete(s->p);
	syntax_release(NULL, UINT32_MAX);
	free(s->cache);
	free(s);
}

static TSPoint point(const Text *t, size_t pos)
{
	size_t r = text_line_of(t, pos);
	return (TSPoint){ (uint32_t)r, (uint32_t)(pos - text_line_start(t, r)) };
}

static int le(TSPoint a, TSPoint b) { return a.row < b.row || (a.row == b.row && a.column <= b.column); }

/* LanguageTree:_edit: edit every tree and region; a region meeting the edit
   (or the whole text) must be parsed again.  Like Range.intercepts there, it
   compares the region's edited points with the edit's old ones, so a region
   that now starts where the deleted text ended stays valid. */
static void edit_ltree(LTree *L, const TSInputEdit *e)
{
	for (uint32_t i = 0; i < L->nreg; i++) {
		Region *g = &L->reg[i];
		if (g->tree) ts_tree_edit(g->tree, e);
		int hit = !g->n;
		for (uint32_t k = 0; k < g->n; k++) {
			ts_range_edit(&g->r[k], e);
			hit |= !le(g->r[k].end_point, e->start_point) && !le(e->old_end_point, g->r[k].start_point);
		}
		if (hit) g->valid = 0;
	}
	for (uint32_t i = 0; i < L->nkid; i++) edit_ltree(L->kid[i], e);
}

static Region edited;      /* s->susp of a parse whose text changed */

void syntax_edit(Syntax *s, const Text *t, size_t pos, size_t ndel, const char *ins, size_t nins)
{
	if (!s) return;
	if (s->sql) {
		tsql_edit(s->sql, t, pos, ndel, nins);
		return;
	}
	s->dirty = 1;
	s->t = t;
	/* the cached faces follow the text, and the bytes around the edit are
	   seeds; an edit at the window's edge or one that outgrows the cache
	   empties it */
	uint32_t p = (uint32_t)pos, d = (uint32_t)ndel, i = (uint32_t)nins;
	if (!s->cz || p < s->ca || p + d > s->cz || s->cz - d + i - s->ca > s->cap) s->cz = 0;
	else {
		memmove(s->cache + (p + i - s->ca), s->cache + (p + d - s->ca), s->cz - p - d);
		memset(s->cache + (p - s->ca), 0, i);
		s->cz = s->cz - d + i;
	}
	/* the window served moves with the text, so the next fill still spans
	   it (else its last line gets a query of its own) */
	s->ra = moved(s->ra, p, d, i), s->rz = moved(s->rz, p, d, i);
	if (s->sa < s->sz) {
		s->sa = moved(s->sa, p, d, i), s->sz = moved(s->sz, p, d, i);
		if ((p ? p - 1 : 0) < s->sa) s->sa = p ? p - 1 : 0;
		if (p + i + 1 > s->sz) s->sz = p + i + 1;
	} else s->sa = p ? p - 1 : 0, s->sz = p + i + 1;
	/* the text a suspended parse was reading has changed, so no region may
	   resume it; parse() drops it after asking stop, because freeing it
	   (ts_parser_reset) can take a millisecond the frame may not have */
	if (s->susp) s->susp = &edited;
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
typedef struct Src { const Text *t; size_t base, n; char w[256]; } Src;

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
static int test1(Src *src, const Pred *p, TSNode n)
{
	uint32_t a = ts_node_start_byte(n), z = ts_node_end_byte(n);
	switch (p->kind) {
	case P_EQ:
		return p->nstr && text_eq(src, a, z, p->str[0], p->len[0]);
	case P_MATCH:
		return p->nstr && lmatch_find(at, src, a, z, p->str[0], p->len[0]);
	case P_ANYOF:
		for (uint32_t k = 0; k < p->nstr; k++)
			if (text_eq(src, a, z, p->str[k], p->len[k])) return 1;
		return 0;
	case P_CONTAINS:
		/* every string, as a plain substring of the node's text */
		for (uint32_t k = 0; k < p->nstr; k++) {
			uint32_t i = a;
			while (i + p->len[k] <= z && !text_eq(src, i, i + p->len[k], p->str[k], p->len[k])) i++;
			if (i + p->len[k] > z) return 0;
		}
		return 1;
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

/* A predicate over every node of its capture, none passing vacuously
   (query.lua's handlers).  Neovim's any-of?, has-ancestor? and has-parent?
   need one node, not all, but in these languages' queries every predicate
   tests a capture of exactly one node (ts_query_capture_quantifier_for_id:
   63 of 63 with Rust), where the two agree. */
static int test(Src *src, const TSQueryMatch *m, const Pred *p)
{
	for (uint16_t i = 0; i < m->capture_count; i++)
		if (m->captures[i].index == p->cap && !test1(src, p, m->captures[i].node)) return 0;
	return 1;
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

/* get_node_ranges: the node's range without its named children (with
   injection.include-children inject() takes the whole range instead) */
static int node_ranges(TSNode n, Ranges *out)
{
	TSPoint sp = ts_node_start_point(n), ep = ts_node_end_point(n);
	uint32_t sb = ts_node_start_byte(n), eb = ts_node_end_byte(n);
	uint32_t nc = ts_node_named_child_count(n);
	if (!nc) return push(out, mkrange(sb, sp, eb, ep));
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
typedef struct { int lang; Ranges r; } Inj;
typedef struct { Inj *v; uint32_t n, cap; } Injs;

static Inj *inj_new(Injs *I, int lang)
{
	if (I->n == I->cap) {
		uint32_t c = I->cap ? 2 * I->cap : 8;
		Inj *b = realloc(I->v, c * sizeof *b);
		if (!b) return NULL;
		I->v = b;
		I->cap = c;
	}
	Inj *j = &I->v[I->n++];
	*j = (Inj){ lang, { NULL, 0, 0 } };
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
			s->cz = 0;          /* faces are clipped to the ranges */
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
		ts_query_cursor_set_byte_range(s->qc, a, z);
		ts_query_cursor_exec(s->qc, Q->q, ts_tree_root_node(L->reg[ti].tree));
		TSQueryMatch m;
		while (!rc && ts_query_cursor_next_match(s->qc, &m)) {
			const Pattern *P = &Q->pat[m.pattern_index];
			if (!passes(&src, &m, P)) continue;
			int lang = P->iself ? L->lang : P->ilang ? resolve(&src, P->ilang, 0, P->ilen) : -1;
			cr.n = 0;
			for (uint16_t i = 0; i < m.capture_count && !rc; i++) {
				const TSQueryCapture *c = &m.captures[i];
				uint32_t a = ts_node_start_byte(c->node), z = ts_node_end_byte(c->node);
				if (c->index == Q->langcap) lang = resolve(&src, NULL, a, z);
				else if (c->index == Q->content)
					rc = P->incl ? push(&cr, mkrange(a, ts_node_start_point(c->node), z, ts_node_end_point(c->node)))
					             : node_ranges(c->node, &cr);
			}
			if (rc || lang < 0 || !cr.n) continue;
			Inj *j = inj_new(&I, lang);
			if (!j) rc = -1;
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
			if (!(K = ltree(lang))) { rc = -1; break; }
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
		L->pa = a;
		L->pz = z;
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
			/* the suspended region left the window or its text changed */
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
		if (g->tree) syntax_release(g->tree, 0);
		g->tree = n;
		s->cz = 0;
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
	if (!s) return 0;
	if (s->sql) return tsql_update(s->sql, stop);
	/* the trees the last parse replaced, freed in steps (release.c) */
	while (syntax_release(NULL, stop ? RELEASE_STEP : UINT32_MAX))
		if (stop()) return 2;
	if (!s->dirty) return 0;
	if (!started(s)) {
		int li = (int)(intptr_t)s->root - 1;
		LTree *R = ltree(li);
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
	if (s->va < s->vz) {
		/* whole lines, as iter_matches(root, source, start_row, end_row + 1) */
		s->ra = (uint32_t)text_line_start(t, text_line_of(t, s->va));
		s->rz = (uint32_t)text_line_start(t, text_line_of(t, s->vz - 1) + 1);
		s->va = UINT32_MAX;
		s->vz = 0;
		unsigned char *c;
		if (s->rz - s->ra > s->cap && s->rz - s->ra <= WINDOW && (c = malloc(2 * (size_t)(s->rz - s->ra)))) {
			free(s->cache);
			s->cache = c;
			s->cap = s->rz - s->ra;
			s->prio = c + s->cap;
			s->cz = 0;
		}
	}
	int r = parse(s, s->root, t);
	if (r == 2) return 2;
	s->dirty = 0;
	return r == 1;
}

/* ---- colours (the redraw path: no allocation) ---- */

/* Faces of [a, z) into the cache, by a query of [q, z) (q <= a: see
   cache) */
static void faces_tree(Syntax *s, const Text *t, LTree *L, uint32_t q, uint32_t a, uint32_t z)
{
	Q *Q = &queries[L->lang].hl;
	for (uint32_t ri = 0; Q->q && ri < L->nreg; ri++) {
		Region *g = &L->reg[ri];
		if (!g->tree) continue;
		if (g->n && (g->r[0].start_byte >= z || g->r[g->n - 1].end_byte <= a)) continue;
		ts_query_cursor_set_byte_range(s->qc, q, z);
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
			int pr = prio_rank(P->prio) + 1;
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
					if (pr >= s->prio[i - s->ca]) s->prio[i - s->ca] = (unsigned char)pr, s->cache[i - s->ca] = f;
			}
		}
	}
	for (uint32_t i = 0; i < L->nkid; i++) faces_tree(s, t, L->kid[i], q, a, z);
}

/* Widens [*a, *z) to every capture with a face in a match that has a
   capture over it, predicates aside: a predicate reads all of a node's
   text, and gates every capture of its match. */
static void widen(Syntax *s, LTree *L, uint32_t *a, uint32_t *z)
{
	Q *Q = &queries[L->lang].hl;
	uint32_t sa = s->sa, sz = s->sz;
	for (uint32_t ri = 0; Q->q && ri < L->nreg; ri++) {
		Region *g = &L->reg[ri];
		if (!g->tree || (g->n && (g->r[0].start_byte >= sz || g->r[g->n - 1].end_byte <= sa))) continue;
		ts_query_cursor_set_byte_range(s->qc, sa, sz);
		ts_query_cursor_exec(s->qc, Q->q, ts_tree_root_node(g->tree));
		TSQueryMatch m;
		/* whole matches: next_capture hands out a match before its
		   captures past the range are found (measured: a Rust macro's
		   name without its "!") */
		while (ts_query_cursor_next_match(s->qc, &m))
			for (uint16_t k = 0; k < m.capture_count; k++) {
				TSNode n = m.captures[k].node;
				if (!Q->face[m.captures[k].index]) continue;
				if (ts_node_start_byte(n) < *a) *a = ts_node_start_byte(n);
				if (ts_node_end_byte(n) > *z) *z = ts_node_end_byte(n);
			}
	}
	for (uint32_t i = 0; i < L->nkid; i++) widen(s, L->kid[i], a, z);
}

void syntax_faces(Syntax *s, size_t a, size_t z, unsigned char *face)
{
	memset(face, 0, z - a);
	if (!s || a >= z) return;
	if (s->sql) {
		tsql_faces(s->sql, a, z, face);
		return;
	}
	if (a < s->va) s->va = (uint32_t)a;
	if (z > s->vz) s->vz = (uint32_t)z;
	/* bytes whose injections are not known yet: the next update finds them */
	if (a < s->ra || z > s->rz) s->dirty = 1;
	/* past syntax.h's 8,192 (the tests' way to show the whole text): no
	   faces, only the window */
	if (!started(s) || z - a > s->cap) return;
	if (a >= s->ca && z <= s->cz) {
		if (s->sa < s->sz) {
			uint32_t ra = s->sa, rz = s->sz;
			widen(s, s->root, &ra, &rz);
			if (ra < s->ca) ra = s->ca;
			if (rz > s->cz) rz = s->cz;
			s->sa = s->sz = 0;
			if (ra < rz) {
				memset(s->cache + (ra - s->ca), 0, rz - ra);
				memset(s->prio + (ra - s->ca), 0, rz - ra);
				faces_tree(s, s->t, s->root, s->ca, ra, rz);
			}
		}
		memcpy(face, s->cache + (a - s->ca), z - a);
		return;
	}
	/* a window as large as the last one shown, from here: the core asks
	   from the window's first line */
	size_t w = s->rz - s->ra, n = text_len(s->t);
	if (w > s->cap) w = w > WINDOW ? CHUNK : s->cap;
	s->ca = (uint32_t)a;
	s->cz = (uint32_t)(a + w > n ? n : a + w);
	if (s->cz < z) s->cz = (uint32_t)z;
	memset(s->cache, 0, s->cz - s->ca);
	memset(s->prio, 0, s->cz - s->ca);
	s->sa = s->sz = 0;
	faces_tree(s, s->t, s->root, s->ca, s->ca, s->cz);
	memcpy(face, s->cache + (a - s->ca), z - a);
}

/* render-markdown 8.14's facts for line l, from the Markdown block tree as
   it reads them: a list marker with its nesting (node:level_in('list')),
   its place among its list_item siblings (sibling_count) and checkbox
   (task_list_marker_*, or the custom [-] after the marker); a heading's
   atx_hN_marker, alone or after the list marker ("- # x"); a
   fenced_code_block's delimiters, info_string, language and background
   rows, unless its range spans 1 row or fewer (Node:height() 2 or less),
   which render-markdown skips.  The node is the
   one at the line's first byte past its indent and block quote markers. */
static size_t blank(const char *b, size_t n, size_t i)
{
	while (i < n && (b[i] == ' ' || b[i] == '\t' || b[i] == '>')) i++;
	return i;
}

int syntax_md(Syntax *s, const Text *t, size_t l, SyntaxMd *o)
{
	memset(o, 0, sizeof *o);
	if (!s || s->sql || !started(s) || s->root->lang != 2 || !s->root->reg->tree) return 0;
	TSNode r = ts_tree_root_node(s->root->reg->tree);
	size_t ls = text_line_start(t, l), len = text_line_start(t, l + 1) - ls - (l < text_lines(t));
	char b[256];
	size_t n = text_read(t, ls, b, len < sizeof b ? len : sizeof b), i = blank(b, n, 0);
	TSNode x = ts_node_descendant_for_byte_range(r, (uint32_t)(ls + i), (uint32_t)(ls + i));
	const char *ty = ts_node_type(x);
	/* list markers ("- - x" holds two: the first is the item's), then what
	   follows them on the line: a heading */
	for (size_t e; !strncmp(ty, "list_marker_", 12); ty = ts_node_type(x)) {
		e = ts_node_end_byte(x) - ls;
		if (!o->list) {
			for (TSNode a = ts_node_parent(x); !ts_node_is_null(a); a = ts_node_parent(a)) o->list += !strcmp(ts_node_type(a), "list");
			if ((o->ordered = !strcmp(ty + 12, "dot") || !strcmp(ty + 12, "parenthesis")))
				for (TSNode a = ts_node_parent(x); !ts_node_is_null(a) && !strcmp(ts_node_type(a), "list_item"); a = ts_node_prev_sibling(a)) o->index++;
			o->item = ts_node_start_byte(x) - ls, o->iend = e;
			TSNode k = ts_node_next_named_sibling(x);
			const char *kt = ts_node_is_null(k) ? "" : ts_node_type(k);
			if (!strncmp(kt, "task_list_marker_", 17)) o->task = kt[17] == 'c' ? 'x' : ' ', o->task_col = ts_node_start_byte(k) - ls;
			/* shortcut.lua: a shortcut_link "[-]" right after the marker with
			   a blank after it (none at the line's end) */
			else if (e + 4 <= n && !memcmp(b + e, "[-] ", 4)) o->task = '-', o->task_col = e;
		}
		e = blank(b, n, e);
		x = ts_node_descendant_for_byte_range(r, (uint32_t)(ls + e), (uint32_t)(ls + e));
	}
	if (!strncmp(ty, "atx_h", 5) && !strcmp(ty + 6, "_marker"))
		o->head = (unsigned char)(ty[5] - '0'), o->mark = ts_node_start_byte(x) - ls, o->mend = ts_node_end_byte(x) - ls;
	/* code: from the line's last byte, as its first can be a block quote's
	   marker that is the code's own text (an unclosed fence takes "> "
	   lines after it) */
	x = ts_node_descendant_for_byte_range(r, (uint32_t)(ls + (len ? len - 1 : 0)), (uint32_t)(ls + (len ? len - 1 : 0)));
	for (; !ts_node_is_null(x) && strcmp(ts_node_type(x), "fenced_code_block"); x = ts_node_parent(x)) ;
	uint32_t sr = 0, er = 0;
	if (!ts_node_is_null(x) && (er = ts_node_end_point(x).row) - (sr = ts_node_start_point(x).row) > 1) {
		/* code.lua: background(start_row + 1, end_row - 2), from the block's
		   start column on every row */
		if (l > sr && l + 2 <= er) o->code = 2, o->mark = ts_node_start_point(x).column;
		for (uint32_t k = 0, d = 0; k < ts_node_child_count(x); k++) {
			TSNode c = ts_node_child(x, k);
			const char *ct = ts_node_type(c);
			int fence = !strcmp(ct, "fenced_code_block_delimiter");
			d += fence;
			if (ts_node_start_point(c).row != l) continue;
			if (fence)
				o->code = d > 1 ? 3 : 1, o->mark = ts_node_start_byte(c) - ls, o->mend = ts_node_end_byte(c) - ls;
			else if (!strcmp(ct, "info_string")) {
				o->info = ts_node_start_byte(c) - ls, o->ninfo = ts_node_end_byte(c) - ts_node_start_byte(c);
				TSNode g = ts_node_named_child(c, 0);
				if (!ts_node_is_null(g) && !strcmp(ts_node_type(g), "language"))
					o->lang = ts_node_start_byte(g) - ls, o->nlang = ts_node_end_byte(g) - ts_node_start_byte(g);
			}
		}
	}
	return o->head || o->list || o->code;
}
