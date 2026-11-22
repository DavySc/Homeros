/* Syntax colours against Neovim 0.12 (nvim --headless on PATH, running
   tests/test_syntax.lua): random windows of real C, Lua, Markdown, Rust
   (ripgrep's printer), GLSL (glslang's test shaders), Nix (nixpkgs'
   writers), CMake (its modules) and SystemVerilog (Verilator's tests) files, then random
   edits applied incrementally with parses in between.  Neovim replays the same edits and parses, and its highlighter's captures, with
   its predicates, injections and #set! priority, must give the same face
   for every byte (per byte the highest priority wins, the later capture on
   a tie).  The schedule must be the same because tree-sitter's incremental
   result can differ from a fresh parse inside syntax errors.
   A second copy of each case parses in slices, stopped at random points and
   resumed; resuming can also change the tree inside errors (measured, plain
   tree-sitter 0.27), so it must match only where Neovim's trees are free of
   errors.  At the end each line's fold must be Neovim's foldexpr's
   (vim.treesitter.foldexpr(), for Markdown MarkdownFold()).  Lua patterns
   are compared with LuaJIT's string.find.  Not
   tested: combined injections are gathered from the window, Neovim's from
   the whole tree (syntax.c, Inj), which differs only for an injected
   language whose text is unbalanced inside one string and closed in another
   outside the window. */
#include <dirent.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include "check.h"
#include "grid.h"
#include "syntax.h"
#include "text.h"
#include "../src/syntax/lmatch.h"
static size_t r1, r2;   /* syntax_update's fold rows, read where a test needs them */

enum { NL = 8, MAXCASES = 300, EDITS = 25, MAXT = 1 << 15 };
static const char *const ext[NL] = { "c", "lua", "md", "rs", "frag", "nix", "cmake", "sv" };
static int CASES = 40;   /* per language; CASES and SEED in the environment */

static char dir[] = "/tmp/test_syntax_XXXXXX";

static char *slurp(const char *path, size_t *n)
{
	FILE *f = fopen(path, "rb");
	CHECK(f);
	CHECK(!fseek(f, 0, SEEK_END));
	long len = ftell(f);
	CHECK(len >= 0 && !fseek(f, 0, SEEK_SET));
	char *r = malloc((size_t)len + 1);
	CHECK(r && fread(r, 1, (size_t)len, f) == (size_t)len);
	fclose(f);
	r[len] = 0;
	*n = (size_t)len;
	return r;
}

static void spit(const char *path, const char *s, size_t n)
{
	FILE *f = fopen(path, "wb");
	CHECK(f && fwrite(s, 1, n, f) == n);
	fclose(f);
}

/* the corpus: the repo's own sources and design, Neovim's runtime Lua */
static char *corpus[NL][64];
static size_t ncorpus[NL];

/* in name order: readdir's differs between file systems, so a failure on
   CI would test other text than a run here */
static void add_dir(int lang, const char *d, const char *e)
{
	struct dirent **de;
	int nd = scandir(d, &de, NULL, alphasort);
	for (int i = 0; i < nd; i++) {
		size_t l = strlen(de[i]->d_name), el = strlen(e);
		if (ncorpus[lang] < 64 && l > el && !strcmp(de[i]->d_name + l - el, e)) {
			char p[2048];
			size_t n;
			snprintf(p, sizeof p, "%s/%s", d, de[i]->d_name);
			char *c = slurp(p, &n);
			if (n) corpus[lang][ncorpus[lang]++] = c;   /* glslang has an empty.frag */
			else free(c);
		}
		free(de[i]);
	}
	if (nd >= 0) free(de);
}

static const char *const snip[NL][16] = {
	{ "\n", "/*", "*/", "\"", "'", "{", "}", "(", ")", "x", "  ", "\xc3\xa9", "#define A 1\n", "int", "FOO_BAR", ";" },
	{ "\n", "--", "[[", "]]", "\"", "'", "(", ")", "x", "  ", "\xc3\xa9", "local", "function", "end", "--- @param", "vim.cmd" },
	{ "\n", "#", "```", "```c\n", "```lua\n", "*", "_", "`", "[", "](", ")", "- ", "| a |", "\n\n", "\xc3\xa9", "> " },
	{ "\n", "//", "/*", "*/", "\"", "'", "{", "}", "(", ")", "!", "x", "\xc3\xa9", "println!(\"{}\", a);", "fn f<T>(", "r#\"" },
	{ "\n", "//", "/*", "*/", "{", "}", "(", ")", ".", "x", "\xc3\xa9", "#version 450\n", "uniform vec4 c;", "layout(location = 0) ", "gl_FragColor", "#define A 1\n" },
	{ "\n", "#", "/*", "*/", "''", "\"", "{", "}", "=", ";", "x", "${", "let ", "# lua\n", "/* lua */ ''", "pkgs.writeRust \"a\" {} ''" },
	{ "\n", "#", "#[[", "]]", "\"", "${", "}", "(", ")", "x", " ", "\xc3\xa9", "set(", "if(ON)", "endif()", "[=[" },
	{ "\n", "//", "/*", "*/", "\"", "(", ")", ";", "x", " ", "\xc3\xa9", "module m;", "endmodule", "always_ff @(posedge clk)", "`define A 1\n", "logic [7:0] " },
};

static int stops_left;
static int stop_after(void) { return stops_left > 0 && --stops_left == 0; }

/* parse: SLICE_NONE to the end in one go; else in slices stopped after 1 to
   40 checks, to the end (SLICE_END) or left stopped half the time
   (SLICE_MAY_STOP), as when a key arrives and edits.  The whole text is
   shown first, so injections are found everywhere, as Neovim's parse(true)
   does; with SLICE_MAY_STOP half the time only some lines, as when the
   window scrolls between slices. */
enum { SLICE_NONE, SLICE_END, SLICE_MAY_STOP };
static void update(Syntax *s, const Text *t, int how)
{
	static unsigned char f[MAXT];
	size_t a = 0, z = text_len(t), n = text_lines(t);
	if (how == SLICE_MAY_STOP && rnd(2)) {
		size_t l = rnd((unsigned)n);
		a = text_line_start(t, l);
		z = text_line_start(t, l + 1 + rnd(20));
	}
	syntax_faces(s, a, z, f);
	do stops_left = 1 + (int)rnd(40);
	while (syntax_update(s, t, how ? stop_after : NULL, &r1, &r2) == 2 && (how == SLICE_END || rnd(2)));
}

/* as core_draw asks: line by line from the first, so the colours come from
   one window that starts where Neovim's redraw of the whole buffer does */
static void faces_lines(Syntax *s, const Text *t, unsigned char *out)
{
	for (size_t l = 0, n = text_lines(t); l <= n; l++) {
		size_t a = text_line_start(t, l), z = text_line_start(t, l + 1);
		syntax_faces(s, a, z, out + a);
	}
}

/* in random pieces, which start windows anywhere */
static void faces_split(Syntax *s, size_t n, unsigned char *out)
{
	for (size_t a = 0; a < n; ) {
		size_t z = a + 1 + rnd(3000);
		if (z > n) z = n;
		syntax_faces(s, a, z, out + a);
		for (size_t i = a; i < z; i++) CHECK(out[i] < FACE_COUNT);
		a = z;
	}
}

/* the faces as Neovim's side writes them: one char per byte, '\n' kept */
static void render(const Text *t, const unsigned char *f, char *out)
{
	static char b[MAXT];
	size_t n = text_read(t, 0, b, sizeof b);
	for (size_t i = 0; i < n; i++) out[i] = b[i] == '\n' ? '\n' : (char)('0' + f[i]);
	out[n] = 0;
}

/* a failing case's text and edits, so a failure on CI can be replayed
   (nvim --headless --clean -l tests/test_syntax.lua OUT DIR CASE: .ops
   lines are e POS DEL HEX, f for a frame, p for a parse).  The text goes
   out in hex (xxd -r -p turns it back): a word in it such as "error"
   would be what planted.sh reports as the failure */
static void show(const char *dir, int c)
{
	char path[2048];
	size_t n;
	snprintf(path, sizeof path, "%s/%d.%s", dir, c, ext[c % NL]);
	char *b = slurp(path, &n);
	fprintf(stderr, "---- %d.%s, in hex ----\n", c, ext[c % NL]);
	for (size_t i = 0; i < n; i++) fprintf(stderr, i % 32 == 31 || i == n - 1 ? "%02x\n" : "%02x", (unsigned char)b[i]);
	fprintf(stderr, "---- %d.%s.ops ----\n", c, ext[c % NL]);
	free(b);
	snprintf(path + strlen(path), sizeof path - strlen(path), ".ops");
	b = slurp(path, &n);
	fprintf(stderr, "%.*s---- end ----\n", (int)n, b);
	free(b);
}

static char want[MAXCASES * NL][MAXT + 1], wfold[MAXCASES * NL][4096];
static int sliced_same[MAXCASES * NL];

static int tat(void *v, size_t i)
{
	char c;
	return text_read(v, i, &c, 1) ? (unsigned char)c : -1;
}

/* each case's line: find's 0 or 1, then gsub's result in hex or "!" */
static size_t lpat_cases(FILE *pf, char *got)
{
	static const char *const pats[] = {
		"^[A-Z][A-Z0-9_]+$", "^[A-Z][A-Z_0-9]*$", "^__builtin_", "^/[*][*][^*].*[*]/$",
		"^[-][-][-]", "^[-][-](%s?)@", "^[%d]+(%.[%d]+)?$", "^;+%s*format%-ignore%s*$",
		"%a+", "%W", "[^%s]-x", "a.-b", "%bxy", "%f[%w]%w+", "(a)%1", "()b", "[a-c%]]+",
		"x?y*z+", "%", "[a", "%g%G", "$^", "^$", "%u%l%d%p%c%x%s%z",
		"/%*%s*([%w%p]+)%s*%*/", "#%s*([%w%p]+)%s*", "^(a)(b?)",
	};
	static const char alpha[] = "aAbxyzZ_9 -*/.;@%[]()#\t\xc3\xa9";
	/* #gsub!'s replacements, as Nix's injections use "%1" */
	static const char *const reps[] = { "%1", "%0", "<%1>", "%%", "%2", "x", "", "%", "%a" };
	size_t n = 0;
	for (int k = 0; k < 4000; k++) {
		const char *p = pats[rnd(sizeof pats / sizeof *pats)];
		char s[24];
		size_t sn = rnd(sizeof s);
		for (size_t i = 0; i < sn; i++) s[i] = alpha[rnd(sizeof alpha - 1)];
		const char *r = reps[rnd(sizeof reps / sizeof *reps)];
		fprintf(pf, "%s\t%s\t%.*s\n", r, p, (int)sn, s);
		Text *t = text_new(s, sn);
		CHECK(t);
		char g[256];
		long gn = lmatch_gsub(tat, t, 0, text_len(t), p, strlen(p), r, strlen(r), g, sizeof g);
		n += (size_t)sprintf(got + n, "%d", lmatch_find(tat, t, 0, text_len(t), p, strlen(p)));
		if (gn < 0) n += (size_t)sprintf(got + n, "!");
		for (long i = 0; i < gn; i++) n += (size_t)sprintf(got + n, "%02x", (unsigned char)g[i]);
		got[n++] = '\n';
		text_free(t);
	}
	return n;
}

/* Scrolling up while the parse of the top paragraph's inline tree is
   stopped: the next update finds the paragraph above it first, so the
   stopped region's ranges change under it.  For each stop point, the
   colours shown must then be a fresh parse's (no edits: every tree is
   fresh). */
static void scroll_while_stopped(const char *dir)
{
	static char b[MAXT];
	static unsigned char f[MAXT], g[MAXT];
	size_t n = 0;
	for (int p = 0; p < 3; p++) {
		for (int l = 0; l < 12; l++)
			n += (size_t)snprintf(b + n, sizeof b - n, "%c *x* `y` [z](w) _v_ **u** `t` %d\n", 'a' + p, l);
		b[n++] = '\n';
	}
	char path[2048];
	snprintf(path, sizeof path, "%s/scroll.md", dir);
	Text *t = text_new(b, n);
	CHECK(t);
	size_t p1 = text_line_start(t, 13), p2 = text_line_start(t, 26), end = text_len(t);
	Syntax *F = syntax_open(path, t);
	CHECK(F);
	syntax_faces(F, 0, end, g);
	while (syntax_update(F, t, NULL, &r1, &r2) == 2) ;
	syntax_faces(F, 0, end, g);
	for (int k = 1; k < 400; k++) {
		Syntax *s = syntax_open(path, t);
		CHECK(s);
		syntax_faces(s, p1, end, f);                 /* the 2nd and 3rd paragraphs */
		stops_left = k;
		if (syntax_update(s, t, stop_after, &r1, &r2) == 2) {
			syntax_faces(s, 0, p2, f);               /* scrolled up one */
			while (syntax_update(s, t, NULL, &r1, &r2) == 2) ;
			syntax_faces(s, 0, p2, f);
			CHECK(!memcmp(f, g, p2));
		}
		syntax_free(s);
	}
	syntax_free(F);
	text_free(t);
}

/* An edit while a parse is stopped: the stopped parse read the old text, so
   it must not resume on the new one.  C without syntax errors before and
   after, where a reparse equals a fresh parse; each stop point in turn, then
   "x" put into the first line, which the parse has read. */
static void edit_while_stopped(const char *dir)
{
	static char b[MAXT];
	static unsigned char f[MAXT], g[MAXT];
	size_t n = 0;
	for (int l = 0; l < 60; l++)
		n += (size_t)snprintf(b + n, sizeof b - n, "int v%d = %d; /* c */ char *s%d = \"q\";\n", l, l, l);
	char path[2048];
	snprintf(path, sizeof path, "%s/edit.c", dir);
	int stopped = 0;
	for (int k = 1; k < 400; k++) {
		Text *t = text_new(b, n);
		CHECK(t);
		Syntax *s = syntax_open(path, t);
		CHECK(s);
		syntax_faces(s, 0, n, f);
		stops_left = k;
		if (syntax_update(s, t, stop_after, &r1, &r2) == 2) {
			stopped++;
			syntax_edit(s, t, 4, 0, "x", 1);         /* v0 becomes xv0 */
			CHECK(!text_insert(t, 4, "x", 1));
			while (syntax_update(s, t, NULL, &r1, &r2) == 2) ;
			syntax_faces(s, 0, n + 1, f);
			Syntax *F = syntax_open(path, t);
			CHECK(F);
			syntax_faces(F, 0, n + 1, g);
			while (syntax_update(F, t, NULL, &r1, &r2) == 2) ;
			syntax_faces(F, 0, n + 1, g);
			CHECK(!memcmp(f, g, n + 1));
			syntax_free(F);
		}
		syntax_free(s);
		text_free(t);
	}
	CHECK(stopped > 10);
}

/* The frame after a key recomputes only what its edits can change, and
   must show what a full recompute over the same trees does: A draws
   between the parse and the edits (so it recomputes part), B does not (so
   its cache is empty and it fills the window). */
static void edit_then_frame(const char *dir, const char *name, const char *b, int ne,
                            const size_t *pos, const size_t *del, const char *const *ins)
{
	static unsigned char f[1024], g[1024];
	size_t n = strlen(b);
	char path[2048];
	snprintf(path, sizeof path, "%s/%s", dir, name);
	Text *t = text_new(b, n);
	CHECK(t);
	Syntax *A = syntax_open(path, t), *B = syntax_open(path, t);
	CHECK(A && B);
	syntax_faces(A, 0, n, f);
	syntax_faces(B, 0, n, g);
	while (syntax_update(A, t, NULL, &r1, &r2) == 2) ;
	while (syntax_update(B, t, NULL, &r1, &r2) == 2) ;
	syntax_faces(A, 0, n, f);
	for (int e = 0; e < ne; e++) {
		syntax_edit(A, t, pos[e], del[e], ins[e], strlen(ins[e]));
		syntax_edit(B, t, pos[e], del[e], ins[e], strlen(ins[e]));
		CHECK(!text_delete(t, pos[e], del[e]) && !text_insert(t, pos[e], ins[e], strlen(ins[e])));
	}
	n = text_len(t);
	syntax_faces(A, 0, n, f);
	syntax_faces(B, 0, n, g);
	CHECK(!memcmp(f, g, n));
	syntax_free(A);
	syntax_free(B);
	text_free(t);
}

static void edits_then_frame(const char *dir)
{
	/* a predicate gates every capture of its match: "find" into "fin"
	   takes string.regexp off the string, which the edit does not touch */
	edit_then_frame(dir, "far.lua", "local a = s:find(\"%d+\")\n", 1,
	                (size_t[]){ 15 }, (size_t[]){ 1 }, (const char *const[]){ "" });
	/* two edits before one frame, the later at byte 0: the bytes to
	   recompute start there (review of PR 42, finding 1) */
	edit_then_frame(dir, "zero.c", "// a\n// b\n", 2,
	                (size_t[]){ 8, 0 }, (size_t[]){ 1, 1 }, (const char *const[]){ "c", "/" });
	/* where captures of one priority overlap (the "*" is @operator and in
	   @variable.parameter), their order depends on where the query starts,
	   so the recompute queries from where the fill did (finding 2) */
	edit_then_frame(dir, "order.c", "d f(u*s)\n*A;\n", 1,
	                (size_t[]){ 6 }, (size_t[]){ 1 }, (const char *const[]){ "" });
	/* "z" into "assert" takes keyword.exception off the whole match,
	   whose "!" lies past the edited bytes */
	edit_then_frame(dir, "macro.rs", "fn f() { assert_eq!(a); }\n", 1,
	                (size_t[]){ 10 }, (size_t[]){ 0 }, (const char *const[]){ "z" });
}

/* An edit that deletes the line before an injected region ends where the
   region now starts: Neovim's LanguageTree:_edit compares rows and columns,
   finds no overlap and keeps the region's tree without a reparse.  Here a
   reparse would lose the emphasis that Neovim shows (as does a fresh
   parse, which the check compares with). */
static void edit_before_region(const char *dir)
{
	static unsigned char f[64], g[64];
	char path[2048];
	snprintf(path, sizeof path, "%s/region.md", dir);
	const char *b = "a\n (D\n\ny,_)_\n";
	Text *t = text_new(b, strlen(b));
	CHECK(t);
	Syntax *A = syntax_open(path, t);
	CHECK(A);
	syntax_faces(A, 0, strlen(b), f);
	while (syntax_update(A, t, NULL, &r1, &r2) == 2) ;
	syntax_edit(A, t, 2, 4, "- ", 2);
	CHECK(!text_delete(t, 2, 4) && !text_insert(t, 2, "- ", 2));
	size_t n = text_len(t);
	syntax_faces(A, 0, n, f);
	while (syntax_update(A, t, NULL, &r1, &r2) == 2) ;
	syntax_faces(A, 0, n, f);
	Syntax *F = syntax_open(path, t);
	CHECK(F);
	syntax_faces(F, 0, n, g);
	while (syntax_update(F, t, NULL, &r1, &r2) == 2) ;
	syntax_faces(F, 0, n, g);
	CHECK(!memcmp(f, g, n));
	syntax_free(A);
	syntax_free(F);
	text_free(t);
}

/* A window past the cache's 8 KB grows the cache in syntax_update; the new
   memory holds no faces yet, so the faces cached for the smaller window
   before must go with the old memory. */
static void cache_grows(const char *dir)
{
	static char b[MAXT];
	static unsigned char f[64], g[64], w[16384];
	size_t n = 0;
	for (int l = 0; l < 1000; l++)
		n += (size_t)snprintf(b + n, sizeof b - n, "int v%d = %d; /* c */\n", l, l);
	char path[2048];
	snprintf(path, sizeof path, "%s/grow.c", dir);
	Text *t = text_new(b, n);
	CHECK(t && n > 16384);
	Syntax *s = syntax_open(path, t), *F = syntax_open(path, t);
	CHECK(s && F);
	syntax_faces(s, 0, 64, f);
	while (syntax_update(s, t, NULL, &r1, &r2) == 2) ;
	syntax_faces(s, 0, 64, f);                   /* cached for a small window */
	syntax_faces(s, 0, sizeof w, w);             /* past the cache */
	while (syntax_update(s, t, NULL, &r1, &r2) == 2) ;     /* grows the cache */
	syntax_faces(s, 0, 64, f);
	syntax_faces(F, 0, 64, g);
	while (syntax_update(F, t, NULL, &r1, &r2) == 2) ;
	syntax_faces(F, 0, 64, g);
	CHECK(!memcmp(f, g, 64));
	syntax_free(s);
	syntax_free(F);
	text_free(t);
}

/* every row of s answers as a table built fresh from t */
static void same_as_fresh(Syntax *s, const Text *t, const char *path)
{
	Syntax *f = syntax_open(path, t);
	CHECK(f);
	while (syntax_update(f, t, NULL, &r1, &r2) == 2) ;
	for (size_t l = 0; l < text_lines(t); l++) {
		int a, b;
		CHECK(syntax_foldexpr(s, l, &a) == syntax_foldexpr(f, l, &b) && a == b);
	}
	syntax_free(f);
}

/* Folds follow an edit: a C function's body folds, and after the edit that
   makes it one line, nothing does. */

static void folds_follow_edits(const char *dir)
{
	char path[2048];
	snprintf(path, sizeof path, "%s/f.c", dir);
	Text *t = text_new("int f() {\n\ta;\n}\n", 16);
	CHECK(t);
	Syntax *s = syntax_open(path, t);
	CHECK(s);
	int st;
	CHECK(!syntax_foldexpr(s, 0, &st) && !st);   /* nothing before the first parse */
	CHECK(syntax_update(s, t, NULL, &r1, &r2) == 1 && r1 == 0 && r2 == 3);
	CHECK(syntax_foldexpr(s, 0, &st) == 1 && st && syntax_foldexpr(s, 2, &st) == 1 && !st);
	for (int i = 0; i < 2; i++) {     /* its two line breaks */
		size_t at = i ? 12 : 9;
		syntax_edit(s, t, at, 1, "", 0);
		CHECK(!text_delete(t, at, 1));
	}
	/* the rows went with the line breaks; the level stays until the parse */
	CHECK(syntax_foldexpr(s, 0, &st) == 1 && st && !syntax_foldexpr(s, 1, &st));
	CHECK(syntax_update(s, t, NULL, &r1, &r2) == 1 && r1 == 0 && r2 == 1);
	CHECK(syntax_foldexpr(s, 0, &st) == 0 && !st);
	/* a line opened above another: "-1" until the parse, and the edited
	   rows move with a later edit above them (the tree is the same, so
	   only they are computed again) */
	syntax_edit(s, t, 0, 0, "int a;\nint b;\n", 14);
	CHECK(!text_insert(t, 0, "int a;\nint b;\n", 14));
	while (syntax_update(s, t, NULL, &r1, &r2) == 2) ;
	for (size_t at = 7; ; at = 0) {
		syntax_edit(s, t, at, 0, "\n", 1);
		CHECK(!text_insert(t, at, "\n", 1));
		if (!at) break;
	}
	CHECK(syntax_foldexpr(s, 0, &st) == -1 && syntax_foldexpr(s, 2, &st) == -1);
	CHECK(syntax_update(s, t, NULL, &r1, &r2) == 1 && r1 == 0 && r2 == 4);
	for (size_t l = 0; l < 4; l++) CHECK(syntax_foldexpr(s, l, &st) == 0);
	syntax_free(s);
	text_free(t);
	/* a whole line deleted takes its own row, not the one below */
	CHECK((t = text_new("x;\nint f() {\n\ta;\n}\n", 19)) && (s = syntax_open(path, t)));
	while (syntax_update(s, t, NULL, &r1, &r2) == 2) ;
	syntax_edit(s, t, 0, 3, "", 0);
	CHECK(!text_delete(t, 0, 3));
	CHECK(syntax_foldexpr(s, 0, &st) == 1 && st);
	syntax_free(s);
	text_free(t);
	/* a line added among the closing braces 21 deep: its rows count from
	   the level past 'foldnestmax', not from the 20 shown */
	static char deep[256];
	size_t dn = 0;
	for (int i = 0; i < 24; i++) dn += (size_t)snprintf(deep + dn, sizeof deep - dn, "%s{\n", i ? "" : "void f() ");
	for (int i = 0; i < 24; i++) deep[dn++] = '}', deep[dn++] = '\n';
	CHECK((t = text_new(deep, dn)) && (s = syntax_open(path, t)));
	while (syntax_update(s, t, NULL, &r1, &r2) == 2) ;
	size_t at = text_line_start(t, 27);
	syntax_edit(s, t, at, 0, "a;\n", 3);
	CHECK(!text_insert(t, at, "a;\n", 3));
	while (syntax_update(s, t, NULL, &r1, &r2) == 2) ;
	same_as_fresh(s, t, path);
	syntax_free(s);
	text_free(t);
	/* the rows a parse stopped in an injection named move with an edit
	   above them before it finishes (a fold typed, the #define's region
	   made invalid, then a line opened at the top or two deleted) */
	const char *src = "#define A(x) x + 1\n\n\n\n\nint f() ;\n\ta;\n\tb;\n}\n";
	for (int k = 2; k < 120; k++) {
		CHECK((t = text_new(src, strlen(src))) && (s = syntax_open(path, t)));
		unsigned char fc[64];
		syntax_faces(s, 0, sizeof fc, fc);
		while (syntax_update(s, t, NULL, &r1, &r2) == 2) ;
		syntax_edit(s, t, 31, 1, "{", 1);
		CHECK(!text_delete(t, 31, 1) && !text_insert(t, 31, "{", 1));
		syntax_edit(s, t, 13, 1, "y", 1);
		CHECK(!text_delete(t, 13, 1) && !text_insert(t, 13, "y", 1));
		stops_left = k / 2;
		syntax_update(s, t, stop_after, &r1, &r2);
		size_t pos = k % 2 ? 0 : 19, del = k % 2 ? 0 : 2;   /* rows 1 and 2 go */
		syntax_edit(s, t, pos, del, "\n", k % 2);
		CHECK(!text_delete(t, pos, del) && !text_insert(t, pos, "\n", k % 2));
		while (syntax_update(s, t, NULL, &r1, &r2) == 2) ;
		same_as_fresh(s, t, path);
		syntax_free(s);
		text_free(t);
	}
	/* a level below 0 (_fold.lua's base is the row above as last
	   computed, so folds that left before the rows recomputed count):
	   Neovim answers ">-1" (measured, 4 runs of test_syntax.lua) */
	snprintf(path, sizeof path, "%s/f.lua", dir);
	const char *lua =
		"  termcap = function(_path, _bufnr)\n"
		"  end,\n"
		"  ['/etc/protocols'] = 'protocols',\n"
		"  INDEX = detect.psf,\n"
		"  ['.pythonstartup'] = 'python',\n"
		"  ['requirements.in'] = 'requirements',\n"
		"  ['resolv.conf'] = 'resolv',\n"
		"  ['robots.txt'] = 'robots',\n"
		"  Brewfile = 'ruby',\n"
		"  Gemfile = 'ruby',\n";
	CHECK((t = text_new(lua, strlen(lua))) && (s = syntax_open(path, t)));
	static unsigned char fl[512];
	syntax_faces(s, 0, text_len(t), fl);
	while (syntax_update(s, t, NULL, &r1, &r2) == 2) ;
	static const struct { size_t pos, del; const char *ins; } le[] = {
		{ 118, 5, "end" }, { 230, 4, "--- @param" }, { 0, 0, NULL }, { 271, 0, "'" }, { 75, 6, "]]" },
	};
	for (size_t i = 0; i < sizeof le / sizeof *le; i++) {
		if (!le[i].ins) {
			syntax_faces(s, 0, text_len(t), fl);
			while (syntax_update(s, t, NULL, &r1, &r2) == 2) ;
			continue;
		}
		syntax_edit(s, t, le[i].pos, le[i].del, le[i].ins, strlen(le[i].ins));
		CHECK(!text_delete(t, le[i].pos, le[i].del) && !text_insert(t, le[i].pos, le[i].ins, strlen(le[i].ins)));
	}
	syntax_faces(s, 0, text_len(t), fl);
	while (syntax_update(s, t, NULL, &r1, &r2) == 2) ;
	static const int want[] = { 3, 2, 0, 3, 3, 1, -1, -2 };   /* >1 1 0 >1 >1 >0 >-1 -1 */
	for (size_t l = 0; l < sizeof want / sizeof *want; l++) {
		int v = syntax_foldexpr(s, l, &st);
		CHECK(2 * v + st == want[l]);
	}
	syntax_free(s);
	text_free(t);
	/* Markdown: an edit inside a line changes the level of the line
	   above (an underline typed), and of the lines below until one keeps
	   its own */
	snprintf(path, sizeof path, "%s/f.md", dir);
	CHECK((t = text_new("```\na\nb\n## h\nx\ny\n```\n", 21)) && (s = syntax_open(path, t)));
	while (syntax_update(s, t, NULL, &r1, &r2) == 2) ;
	CHECK(!syntax_foldexpr(s, 1, &st) && syntax_foldexpr(s, 5, &st) == 2);
	syntax_edit(s, t, 6, 1, "=", 1);
	CHECK(!text_delete(t, 6, 1) && !text_insert(t, 6, "=", 1));
	syntax_edit(s, t, 8, 3, "", 0);
	CHECK(!text_delete(t, 8, 3));
	while (syntax_update(s, t, NULL, &r1, &r2) == 2) ;
	CHECK(syntax_foldexpr(s, 1, &st) == 1 && st && syntax_foldexpr(s, 5, &st) == 1);
	syntax_free(s);
	text_free(t);
	/* an underline's trailing space deleted: the tree is the same (a
	   setext heading either way), MarkdownFold() is not */
	CHECK((t = text_new("a\n=== \nx\n", 9)) && (s = syntax_open(path, t)));
	while (syntax_update(s, t, NULL, &r1, &r2) == 2) ;
	CHECK(!syntax_foldexpr(s, 0, &st));
	syntax_edit(s, t, 5, 1, "", 0);
	CHECK(!text_delete(t, 5, 1));
	while (syntax_update(s, t, NULL, &r1, &r2) == 2) ;
	CHECK(syntax_foldexpr(s, 0, &st) == 1 && st && syntax_foldexpr(s, 2, &st) == 1);
	syntax_free(s);
	text_free(t);
	/* a fence typed changes the tree to the end, but MarkdownFold() only
	   its own row: the rows reported stop where a level is as before */
	CHECK((t = text_new("a\n\nb\nc\nd\n", 10)) && (s = syntax_open(path, t)));
	while (syntax_update(s, t, NULL, &r1, &r2) == 2) ;
	syntax_edit(s, t, 0, 0, "```", 3);
	CHECK(!text_insert(t, 0, "```", 3));
	while (syntax_update(s, t, NULL, &r1, &r2) == 2) ;
	CHECK(r1 == 0 && r2 <= 2);
	syntax_free(s);
	text_free(t);
}

/* The trees a reparse replaces are freed in steps that ask stop: an edit
   that deletes 2,000 lines frees about 10,000 nodes, so the update
   after it must stop more than once before it is done. */
static int always(void) { return 1; }
/* A `define whose macro_text is empty and ends the text injects a region
   with no ranges, which is the whole text, in SystemVerilog again:
   without a stop the parse recurses until the stack overflows (Neovim's
   Lua stack does, so there is no Neovim answer to compare) */
static void self_injection(const char *dir)
{
	char path[2048];
	const char *src = "`define A`define A`define A`define A\n";
	snprintf(path, sizeof path, "%s/self.sv", dir);
	Text *t = text_new(src, strlen(src));
	Syntax *s = t ? syntax_open(path, t) : NULL;
	CHECK(s);
	unsigned char f[64];
	syntax_faces(s, 0, strlen(src), f);   /* injections are found in the window */
	while (syntax_update(s, t, NULL, &r1, &r2) == 2) ;
	syntax_free(s);
	text_free(t);
}

static void release_in_steps(const char *dir)
{
	static char b[MAXT];
	size_t n = 0;
	for (int l = 0; l < 2000 && n + 32 < sizeof b; l++)
		n += (size_t)snprintf(b + n, sizeof b - n, "int v%d = %d;\n", l, l);
	char path[2048];
	snprintf(path, sizeof path, "%s/release.c", dir);
	Text *t = text_new(b, n);
	CHECK(t);
	Syntax *s = syntax_open(path, t);
	CHECK(s);
	static unsigned char f[MAXT];
	syntax_faces(s, 0, 100, f);
	CHECK(syntax_update(s, t, NULL, &r1, &r2) == 1);
	syntax_edit(s, t, 0, n - 1, "", 0);
	CHECK(!text_delete(t, 0, n - 1));
	CHECK(syntax_update(s, t, NULL, &r1, &r2) == 1);
	int steps = 0;
	while (syntax_update(s, t, always, &r1, &r2) == 2) steps++;
	CHECK(steps > 1);
	syntax_free(s);
	text_free(t);
	/* a leaf whose scanner state is over 24 bytes keeps it on the heap
	   (ts_external_scanner_state_delete): Markdown with 40 nested block
	   quotes; LeakSanitizer sees it if release.c skips it */
	n = 0;
	for (int l = 0; l < 3; l++) {
		for (int i = 0; i < 40; i++) b[n++] = '>', b[n++] = ' ';
		memcpy(b + n, "text\n", 5), n += 5;
	}
	snprintf(path, sizeof path, "%s/quotes.md", dir);
	CHECK((t = text_new(b, n)) && (s = syntax_open(path, t)));
	syntax_faces(s, 0, n, f);
	while (syntax_update(s, t, NULL, &r1, &r2)) ;
	syntax_edit(s, t, 0, n - 1, "", 0);
	CHECK(!text_delete(t, 0, n - 1));
	while (syntax_update(s, t, NULL, &r1, &r2)) ;
	syntax_free(s);
	text_free(t);
}

/* syntax_filetype against Neovim's vim.filetype.match, for each ending
   it knows and some it does not: 0 differ, or the test fails */
static void filetypes(void)
{
	static const char *const names[] = {
		"d/a.c", "a.h", "a.lua", "a.md", "a.sql", "a.json", "a.txt", "a.xyz", "x.sql/b", "a.c.txt", "a.sql.md", "a.mdx", "Makefile.c",
		".sql", "a\\b.sql", "CMakeLists.txt", "x/CMakeLists.txt", "a\\CMakeLists.txt", "fooCMakeLists.txt", "a.rs", "a.nix",
		"a.glsl", "a.vert", "a.frag", "a.comp", "a.geom", "a.tesc", "a.tese", "a.sv", "a.svh", "a.cmake", "a.lean", "a.cs", "a.org", "a.SQL",
	};
	char cmd[2048] = "nvim --headless --clean -l /dev/stdin <<'E'\nfor _, n in ipairs({", want[64];
	for (size_t i = 0; i < sizeof names / sizeof *names; i++) snprintf(cmd + strlen(cmd), sizeof cmd - strlen(cmd), "[[%s]],", names[i]);
	strcat(cmd, "}) do io.write((vim.filetype.match({filename = n}) or '-'), '\\n') end\nE");
	FILE *p = popen(cmd, "r");
	CHECK(p);
	for (size_t i = 0; i < sizeof names / sizeof *names; i++) {
		CHECK(fgets(want, sizeof want, p));
		want[strcspn(want, "\n")] = 0;
		const char *ft = syntax_filetype(names[i]);
		if (strcmp(ft ? ft : "-", want)) fprintf(stderr, "%s: filetype %s, Neovim %s\n", names[i], ft ? ft : "-", want);
		CHECK(!strcmp(ft ? ft : "-", want));
	}
	CHECK(!pclose(p));
	/* an unnamed buffer */
	CHECK(!syntax_filetype(NULL));
	/* Neovim knows no .dl */
	CHECK(!strcmp(syntax_filetype("x/a.dl"), "souffle"));
	printf("test_syntax: %zu file names' filetypes as Neovim's\n", sizeof names / sizeof *names);
}

/* A .v gets SystemVerilog's colours where Neovim says verilog by its
   content (Coq and V get none): tricky cases and the Verilator corpus */
static void v_files(const char *dir)
{
	static const char *const tricky[] = {
		"module m(input a);\nendmodule\n", "Theorem x : True.\n", "fn main() {\n}\n", "",
		"/* a.\n*/\nmodule m (\n", "// x.\nwire a;\n", "x = 1; // y.\n", "(* keep *) wire a;\n",
		"  /* c */ x;\n", "Module M.\n", "a;\r\n", "module foo_1 (\n", "x // (* y\nw;", "a.  \n",
		"/* x\n (* y *)\n*/\nwire b;\n", "// a;\nTheorem x.\n",
	};
	enum { NT = sizeof tricky / sizeof *tricky };
	char path[2048], *cmd = malloc(1 << 16), ft[64];
	CHECK(cmd);
	int n = 0, nv = NT + (int)ncorpus[7], len = snprintf(cmd, 1 << 16, "cd %s && nvim --headless --clean -l /dev/stdin <<'E'\nfor i = 0, %d do vim.cmd.edit(i .. '.v') io.write(vim.bo.filetype, '\\n') end\nE", dir, nv);
	CHECK(len < 1 << 16);
	for (int i = 0; i <= nv; i++) {
		static char late[500 * 2 + 4];   /* line 501 */
		for (int k = 0; i == NT && k < 501; k++) memcpy(late + 2 * k, k < 500 ? "x\n" : "a;\n", k < 500 ? 2 : 3);
		const char *c = i < NT ? tricky[i] : i == NT ? late : corpus[7][i - NT - 1];
		snprintf(path, sizeof path, "%s/%d.v", dir, i);
		FILE *f = fopen(path, "w");
		CHECK(f && fwrite(c, 1, strlen(c), f) == strlen(c) && !fclose(f));
	}
	FILE *p = popen(cmd, "r");
	CHECK(p);
	for (int i = 0; i <= nv; i++) {
		CHECK(fgets(ft, sizeof ft, p));
		snprintf(path, sizeof path, "%s/%d.v", dir, i);
		size_t l;
		char *c = slurp(path, &l);
		Text *t = text_new(c, l);
		Syntax *s = syntax_open(path, t);
		if (!s != !!strcmp(ft, "verilog\n")) fprintf(stderr, "%d.v: Neovim %s", i, ft);
		CHECK(!s == !!strcmp(ft, "verilog\n"));
		n += !!s;
		syntax_free(s);
		text_free(t);
		free(c);
	}
	CHECK(!pclose(p));
	CHECK(!syntax_filetype("a.v"));
	free(cmd);
	printf("test_syntax: %d .v files, %d of them Verilog, as Neovim's\n", nv + 1, n);
}

int main(void)
{
	if (getenv("SEED")) rng = 2 * strtoull(getenv("SEED"), NULL, 10) + 1;   /* distinct seeds stay distinct */
	if (getenv("CASES")) CASES = atoi(getenv("CASES"));
	CHECK(CASES > 0 && CASES <= MAXCASES);
	CHECK(mkdtemp(dir));
	scroll_while_stopped(dir);
	edit_while_stopped(dir);
	edits_then_frame(dir);
	edit_before_region(dir);
	cache_grows(dir);
	release_in_steps(dir);
	self_injection(dir);
	folds_follow_edits(dir);
	filetypes();
	char path[2048], vrt[1024] = "";
	FILE *p = popen("nvim --headless --clean -l /dev/stdin <<'E'\nio.write(vim.env.VIMRUNTIME)\nE", "r");
	CHECK(p && fgets(vrt, sizeof vrt, p));
	pclose(p);
	add_dir(0, "ref", ".c");
	add_dir(0, "tests", ".c");
	snprintf(path, sizeof path, "%s/lua/vim", vrt);
	add_dir(1, path, ".lua");
	add_dir(2, "design", ".md");
	add_dir(2, ".", ".md");
	add_dir(3, "build/syntax/src/corpus/rust", ".rs");
	add_dir(4, "build/syntax/src/corpus/glsl", ".frag");   /* the first 64 by name */
	add_dir(5, "build/syntax/src/corpus/nix", ".nix");
	add_dir(6, "build/syntax/src/corpus/cmake", ".cmake");      /* the first 64 by name */
	add_dir(7, "build/syntax/src/corpus/systemverilog", ".v");  /* SystemVerilog, read as .sv */
	for (int l = 0; l < NL; l++) CHECK(ncorpus[l]);
	v_files(dir);

	static unsigned char fa[MAXT], fb[MAXT], fs[MAXT], ff[MAXT];
	static char buf[MAXT], sbuf[MAXT + 1], cmd[1 << 16];
	snprintf(path, sizeof path, "%s/want.snap", dir);
	FILE *snap = fopen(path, "w");
	CHECK(snap);
	int len = snprintf(cmd, sizeof cmd, "nvim --headless --clean -l tests/test_syntax.lua %s/out %s", dir, dir);
	size_t bytes = 0;
	int n = CASES * NL;
	for (int c = 0; c < n; c++) {
		int lang = c % NL;
		/* fixed and unedited, so the final check sees them: case 2, inline
		   captures across block quote markers, which the highlighter colours
		   only inside the inline tree's ranges, so the second "> " stays
		   plain; case 3, Rust macros, whose bodies Neovim parses as Rust
		   (injection.include-children) and whose name contains? "assert";
		   case 5, Nix's combined injections: Lua named by a comment
		   (#gsub!) in two strings parsed as one tree, and writeRust's;
		   case 6, Vim's match? in CMake's query: words ignoring case, a
		   suffix after ".*", one word alone (set's variable);
		   case 8, 24 nested C blocks, past 'foldnestmax' (20);
		   case 10, Markdown's folds: setext headings (not under an empty
		   line), and a '#' line in a code block, which MarkdownFold()
		   folds with tree-sitter on;
		   case 13, a Nix string Neovim does not inject, as its injection
		   query's patterns of languages we lack still count toward the 256
		   in-progress matches.
		   The others: up to 80 lines of the corpus from a random line */
		static char deep[300];
		if (!deep[0]) {
			strcpy(deep, "void f() {\n");
			for (int k = 0; k < 24; k++) strcat(deep, "if (a) {\n");
			strcat(deep, "x;\n");
			for (int k = 0; k < 25; k++) strcat(deep, "}\n");
		}
		const char *fx = c == 2 ? "> *a\n> b* `c\n> d` [e\n> f](g)\n"
		               : c == 3 ? "fn f() {\n    assert_eq!(v.len(), 3);\n    println!(\"{}\", g(x).y);\n}\n"
		               : c == 5 ? "{\n  a = /* lua */ ''\n    local t = {\n  '';\n  b = /* lua */ ''\n    }\n  '';\n"
		                          "  c = pkgs.writeRust \"r\" {} ''\n    fn main() { let x = 1; }\n  '';\n}\n"
		               : c == 6 ? "if(A-NOTFOUND AND x-notfound)\n  SET(v ON)\nendif()\nreturn()\n"
		               : c == 8 ? deep
		               : c == 10 ? "\n===\nTitle\n=====\ntext\nSub\n---\n# h\n```\n# not\n```\n## b\n"
		               : c == 13 ? "  m (a // { i = \"${l.e p.b}\"; }) na/* lua */ ''  else\n    L b the ${f l is a s t b\n"
		                           "    C b c w${ or w e a.\n:::{.example}\n    }\n    ''\n" : NULL;
		const char *src = fx ? fx : corpus[lang][rnd((unsigned)ncorpus[lang])];
		size_t sn = strlen(src), a = fx ? 0 : rnd((unsigned)sn), z;
		while (a && src[a - 1] != '\n') a--;
		z = a;
		for (int l = 0; l < 80 && z < sn; z++) l += src[z] == '\n';
		Text *t = text_new(src + a, z - a);
		CHECK(t);
		if (!text_len(t) || (text_read(t, text_len(t) - 1, buf, 1), buf[0] != '\n'))
			CHECK(!text_insert(t, text_len(t), "\n", 1));
		snprintf(path, sizeof path, "%s/%d.%s", dir, c, ext[lang]);
		spit(path, buf, text_read(t, 0, buf, sizeof buf));
		Syntax *A = syntax_open(path, t), *B = syntax_open(path, t);
		CHECK(A && B);
		snprintf(path + strlen(path), sizeof path - strlen(path), ".ops");
		FILE *ops = fopen(path, "w");
		CHECK(ops);
		update(A, t, SLICE_NONE);
		update(B, t, SLICE_MAY_STOP);
		for (int e = fx ? 0 : rnd(EDITS + 1); e--; ) {
			size_t tn = text_len(t), pos = rnd((unsigned)tn), del = rnd(8);
			if (del > tn - 1 - pos) del = tn - 1 - pos;   /* keep the final '\n' */
			const char *ins = snip[lang][rnd(16)];
			if (tn + strlen(ins) >= MAXT - 1) break;
			if (rnd(4)) {
				/* faces from edited trees not yet parsed again: in range */
				size_t fz = tn < 500 ? tn : 500;
				syntax_faces(B, 0, fz, fb);
				for (size_t i = 0; i < fz; i++) CHECK(fb[i] < FACE_COUNT);
			}
			syntax_edit(A, t, pos, del, ins, strlen(ins));
			syntax_edit(B, t, pos, del, ins, strlen(ins));
			CHECK(!text_delete(t, pos, del) && !text_insert(t, pos, ins, strlen(ins)));
			fprintf(ops, "e %zu %zu ", pos, del);
			for (const char *i = ins; *i; i++) fprintf(ops, "%02x", (unsigned char)*i);
			fprintf(ops, "\n");
			if (!rnd(3)) {
				/* the frame drawn after a key, before the parse: edited
				   trees and regions, as Neovim's after on_bytes */
				faces_lines(A, t, fa);
				render(t, fa, sbuf);
				fprintf(snap, "#%d\n%s", c, sbuf);
				fprintf(ops, "f\n");
			}
			if (rnd(2)) {
				update(A, t, SLICE_NONE);
				update(B, t, SLICE_MAY_STOP);
				fprintf(ops, "p\n");
			}
		}
		fclose(ops);
		update(A, t, SLICE_NONE);
		update(B, t, SLICE_END);
		CHECK(syntax_update(A, t, NULL, &r1, &r2) == 0);     /* nothing left to do */
		faces_lines(A, t, fa);
		faces_lines(B, t, fb);
		faces_split(B, text_len(t), fs);
		/* tree-sitter's incremental parse depends on the trees it starts
		   from (in Markdown's inline grammar a reparse can even differ
		   from a fresh parse without any syntax error), so a sliced parse
		   must match either the unsliced one or a fresh parse; bit 1: the
		   faces of windows starting elsewhere agree */
		if (memcmp(fa, fb, text_len(t))) {
			path[strlen(path) - 4] = 0;   /* without ".ops" */
			Syntax *F = syntax_open(path, t);
			CHECK(F);
			update(F, t, SLICE_NONE);
			faces_lines(F, t, ff);
			syntax_free(F);
		} else memcpy(ff, fa, text_len(t));
		sliced_same[c] = (!memcmp(fa, fb, text_len(t)) || !memcmp(ff, fb, text_len(t)))
		                 + 2 * !memcmp(fb, fs, text_len(t));
		render(t, fa, want[c]);
		for (size_t l = 0, k = 0, nl = text_lines(t); l < nl && k < sizeof wfold[c] - 8; l++) {
			int st, v = syntax_foldexpr(A, l, &st);
			k += (size_t)snprintf(wfold[c] + k, sizeof wfold[c] - k, "%s%s%d", l ? " " : "", st ? ">" : "", v);
		}
		bytes += text_len(t);
		len += snprintf(cmd + len, sizeof cmd - (size_t)len, " %d.%s", c, ext[lang]);
		syntax_free(A);
		syntax_free(B);
		text_free(t);
	}
	CHECK((size_t)len < sizeof cmd - 1);
	fclose(snap);

	static char pgot[1 << 20];
	snprintf(path, sizeof path, "%s/out.pat", dir);
	FILE *pf = fopen(path, "w");
	CHECK(pf);
	size_t npg = lpat_cases(pf, pgot);
	fclose(pf);

	CHECK(system(cmd) == 0);

	size_t on, en, pn;
	snprintf(path, sizeof path, "%s/out", dir);
	char *o = slurp(path, &on), *q = o;
	snprintf(path, sizeof path, "%s/out.err", dir);
	char *err = slurp(path, &en);
	CHECK(en == (size_t)n);
	int differ = 0, slice_differ = 0, with_err = 0, slice_err = 0, md_history = 0, fdiffer = 0;
	snprintf(path, sizeof path, "%s/out.fold", dir);
	FILE *fo = fopen(path, "r");
	CHECK(fo);
	for (int c = 0; c < n; c++) {
		size_t wl = strlen(want[c]);
		CHECK((size_t)(q - o) + wl <= on);
		if (strncmp(q, want[c], wl)) {
			differ++;
			for (size_t i = 0, line = 1; i < wl; i++) {
				if (want[c][i] == '\n') { line++; continue; }
				if (q[i] != want[c][i]) {
					fprintf(stderr, "%s/%d.%s line %zu: face %d in Neovim, %d here\n",
					        dir, c, ext[c % NL], line, (unsigned char)q[i] - '0', (unsigned char)want[c][i] - '0');
					show(dir, c);
					break;
				}
			}
		}
		static char fl[1 << 16];
		CHECK(fgets(fl, sizeof fl, fo));
		fl[strcspn(fl, "\n")] = 0;
		if (strcmp(fl, wfold[c]) && ++fdiffer <= 3) {
			fprintf(stderr, "%s/%d.%s: folds differ\n  Neovim %s\n  here   %s\n", dir, c, ext[c % NL], fl, wfold[c]);
			show(dir, c);
		}
		with_err += err[c] == '1';
		if (sliced_same[c] != 3) {
			/* Markdown's inline grammar gives trees that depend on the old
			   tree a reparse starts from even without syntax errors
			   (measured: each such tree equals tree-sitter's own one-go
			   parse from that old tree), so there a sliced parse may differ
			   from both, as long as windows starting elsewhere agree */
			if (err[c] == '1') slice_err++;
			else if (c % NL == 2 && sliced_same[c] == 2) md_history++;
			else {
				slice_differ++;
				fprintf(stderr, "%s/%d.%s: sliced parse or windows starting elsewhere differ, no syntax error\n", dir, c, ext[c % NL]);
				show(dir, c);
			}
		}
		q += wl;
	}
	/* the frames before a parse: "#case" then the faces, from each side */
	size_t wn, sn;
	snprintf(path, sizeof path, "%s/want.snap", dir);
	char *ws = slurp(path, &wn);
	snprintf(path, sizeof path, "%s/out.snap", dir);
	char *ns = slurp(path, &sn);
	int snaps = 0, snap_differ = 0;
	for (char *w = ws, *v = ns; w < ws + wn; snaps++) {
		char *we = memchr(w + 1, '#', (size_t)(ws + wn - w - 1)), *ve;
		if (!we) we = ws + wn;
		CHECK(v < ns + sn);
		ve = memchr(v + 1, '#', (size_t)(ns + sn - v - 1));
		if (!ve) ve = ns + sn;
		if (we - w != ve - v || memcmp(w, v, (size_t)(we - w))) {
			snap_differ++;
			fprintf(stderr, "%s/%.*s: a frame before the parse differs from Neovim's\n", dir,
			        (int)strcspn(w + 1, "\n"), w + 1);
			show(dir, atoi(w + 1));
		}
		w = we, v = ve;
	}
	snprintf(path, sizeof path, "%s/out.patout", dir);
	char *po = slurp(path, &pn);
	int pdiff = 0, plines = 0;
	CHECK(pn && po[pn - 1] == '\n');
	for (char *x = po, *y = pgot; x < po + pn && y < pgot + npg; x = strchr(x, '\n') + 1, y = strchr(y, '\n') + 1, plines++)
		if (strncmp(x, y, (size_t)(strchr(x, '\n') - x + 1)) && ++pdiff == 1)
			fprintf(stderr, "Lua pattern case: LuaJIT %.*s here %.*s\n", (int)(strchr(x, '\n') - x), x, (int)(strchr(y, '\n') - y), y);
	fclose(fo);
	printf("test_syntax: %d of %d cases (%zu bytes) differ from Neovim (folds in %d), and %d of %d frames between an edit "
	       "and its parse; sliced parses or other windows differ in %d of %d cases without syntax errors "
	       "(and %d of %d with; Markdown's history-dependent trees in %d more); %d of 4000 Lua patterns (find, gsub) differ "
	       "from LuaJIT\n",
	       differ, n, bytes, fdiffer, snap_differ, snaps, slice_differ, n - with_err, slice_err, with_err, md_history,
	       pdiff);
	CHECK(plines == 4000);
	CHECK(!differ && !fdiffer && !snap_differ && !slice_differ && !pdiff);
	free(ws);
	free(ns);
	free(o);
	free(err);
	free(po);
	snprintf(cmd, sizeof cmd, "rm -rf %s", dir);
	CHECK(system(cmd) == 0);
	return 0;
}
