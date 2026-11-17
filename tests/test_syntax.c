/* Syntax colours against Neovim 0.12 (nvim --headless on PATH, running
   tests/test_syntax.lua): random windows of real C, Lua and Markdown files,
   then random edits applied incrementally with parses in between.  Neovim
   replays the same edits and parses, and its highlighter's captures, with
   its predicates, injections and #set! priority, must give the same face
   for every byte (per byte the highest priority wins, the later capture on
   a tie).  The schedule must be the same because tree-sitter's incremental
   result can differ from a fresh parse inside syntax errors.
   A second copy of each case parses in slices, stopped at random points and
   resumed; resuming can also change the tree inside errors (measured, plain
   tree-sitter 0.27), so it must match only where Neovim's trees are free of
   errors.  Lua patterns are compared with LuaJIT's string.find. */
#include <dirent.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include "check.h"
#include "grid.h"
#include "syntax.h"
#include "text.h"
#include "../src/syntax/lmatch.h"

enum { NL = 3, MAXCASES = 300, EDITS = 25, MAXT = 1 << 15 };
static const char *const ext[NL] = { "c", "lua", "md" };
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

static void add_dir(int lang, const char *d, const char *e)
{
	DIR *D = opendir(d);
	if (!D) return;
	struct dirent *de;
	while ((de = readdir(D)) && ncorpus[lang] < 64) {
		size_t l = strlen(de->d_name), el = strlen(e);
		if (l <= el || strcmp(de->d_name + l - el, e)) continue;
		char p[2048];
		size_t n;
		snprintf(p, sizeof p, "%s/%s", d, de->d_name);
		corpus[lang][ncorpus[lang]++] = slurp(p, &n);
	}
	closedir(D);
}

static const char *const snip[NL][16] = {
	{ "\n", "/*", "*/", "\"", "'", "{", "}", "(", ")", "x", "  ", "\xc3\xa9", "#define A 1\n", "int", "FOO_BAR", ";" },
	{ "\n", "--", "[[", "]]", "\"", "'", "(", ")", "x", "  ", "\xc3\xa9", "local", "function", "end", "--- @param", "vim.cmd" },
	{ "\n", "#", "```", "```c\n", "```lua\n", "*", "_", "`", "[", "](", ")", "- ", "| a |", "\n\n", "\xc3\xa9", "> " },
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
	while (syntax_update(s, t, how ? stop_after : NULL) == 2 && (how == SLICE_END || rnd(2)));
}

/* as core_draw asks: line by line from the first, so the colours come from
   one window that starts where Neovim's redraw of the whole buffer does */
static void faces_lines(Syntax *s, const Text *t, unsigned char *out)
{
	for (size_t l = 0, n = text_lines(t); l < n; l++) {
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

static char want[MAXCASES * NL][MAXT + 1];
static int sliced_same[MAXCASES * NL];

static int tat(void *v, size_t i)
{
	char c;
	return text_read(v, i, &c, 1) ? (unsigned char)c : -1;
}

static size_t lpat_cases(FILE *pf, char *got)
{
	static const char *const pats[] = {
		"^[A-Z][A-Z0-9_]+$", "^[A-Z][A-Z_0-9]*$", "^__builtin_", "^/[*][*][^*].*[*]/$",
		"^[-][-][-]", "^[-][-](%s?)@", "^[%d]+(%.[%d]+)?$", "^;+%s*format%-ignore%s*$",
		"%a+", "%W", "[^%s]-x", "a.-b", "%bxy", "%f[%w]%w+", "(a)%1", "()b", "[a-c%]]+",
		"x?y*z+", "%", "[a", "%g%G", "$^", "^$", "%u%l%d%p%c%x%s%z",
	};
	static const char alpha[] = "aAbxyzZ_9 -*/.;@%[]()\t\xc3\xa9";
	size_t n = 0;
	for (int k = 0; k < 4000; k++) {
		const char *p = pats[rnd(sizeof pats / sizeof *pats)];
		char s[24];
		size_t sn = rnd(sizeof s);
		for (size_t i = 0; i < sn; i++) s[i] = alpha[rnd(sizeof alpha - 1)];
		fprintf(pf, "%s\t%.*s\n", p, (int)sn, s);
		Text *t = text_new(s, sn);
		CHECK(t);
		got[n++] = (char)('0' + lmatch_find(tat, t, 0, text_len(t), p, strlen(p)));
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
	while (syntax_update(F, t, NULL) == 2) ;
	syntax_faces(F, 0, end, g);
	for (int k = 1; k < 400; k++) {
		Syntax *s = syntax_open(path, t);
		CHECK(s);
		syntax_faces(s, p1, end, f);                 /* the 2nd and 3rd paragraphs */
		stops_left = k;
		if (syntax_update(s, t, stop_after) == 2) {
			syntax_faces(s, 0, p2, f);               /* scrolled up one */
			while (syntax_update(s, t, NULL) == 2) ;
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
		if (syntax_update(s, t, stop_after) == 2) {
			stopped++;
			syntax_edit(s, t, 4, 0, "x", 1);         /* v0 becomes xv0 */
			CHECK(!text_insert(t, 4, "x", 1));
			while (syntax_update(s, t, NULL) == 2) ;
			syntax_faces(s, 0, n + 1, f);
			Syntax *F = syntax_open(path, t);
			CHECK(F);
			syntax_faces(F, 0, n + 1, g);
			while (syntax_update(F, t, NULL) == 2) ;
			syntax_faces(F, 0, n + 1, g);
			CHECK(!memcmp(f, g, n + 1));
			syntax_free(F);
		}
		syntax_free(s);
		text_free(t);
	}
	CHECK(stopped > 10);
}

int main(void)
{
	if (getenv("SEED")) rng = 2 * strtoull(getenv("SEED"), NULL, 10) + 1;   /* distinct seeds stay distinct */
	if (getenv("CASES")) CASES = atoi(getenv("CASES"));
	CHECK(CASES > 0 && CASES <= MAXCASES);
	CHECK(mkdtemp(dir));
	scroll_while_stopped(dir);
	edit_while_stopped(dir);
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
	for (int l = 0; l < NL; l++) CHECK(ncorpus[l]);

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
		/* case 2: inline captures across block quote markers, which the
		   highlighter colours only inside the inline tree's ranges, so the
		   second "> " stays plain (unedited, so the final check sees it); the
		   others: up to 80 lines of the corpus
		   from a random line */
		const char *src = c == 2 ? "> *a\n> b* `c\n> d` [e\n> f](g)\n" : corpus[lang][rnd((unsigned)ncorpus[lang])];
		size_t sn = strlen(src), a = c == 2 ? 0 : rnd((unsigned)sn), z;
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
		for (int e = c == 2 ? 0 : rnd(EDITS + 1); e--; ) {
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
		CHECK(syntax_update(A, t, NULL) == 0);     /* nothing left to do */
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
		bytes += text_len(t);
		len += snprintf(cmd + len, sizeof cmd - (size_t)len, " %d.%s", c, ext[lang]);
		syntax_free(A);
		syntax_free(B);
		text_free(t);
	}
	CHECK((size_t)len < sizeof cmd - 1);
	fclose(snap);

	static char pgot[8192];
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
	int differ = 0, slice_differ = 0, with_err = 0, slice_err = 0, md_history = 0;
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
					break;
				}
			}
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
		}
		w = we, v = ve;
	}
	snprintf(path, sizeof path, "%s/out.patout", dir);
	char *po = slurp(path, &pn);
	CHECK(pn == npg);
	int pdiff = 0;
	for (size_t i = 0; i < npg; i++) pdiff += po[i] != pgot[i];
	printf("test_syntax: %d of %d cases (%zu bytes) differ from Neovim, and %d of %d frames between an edit "
	       "and its parse; sliced parses or other windows differ in %d of %d cases without syntax errors "
	       "(and %d of %d with; Markdown's history-dependent trees in %d more); %d of %zu Lua patterns differ "
	       "from LuaJIT\n",
	       differ, n, bytes, snap_differ, snaps, slice_differ, n - with_err, slice_err, with_err, md_history,
	       pdiff, npg);
	CHECK(!differ && !snap_differ && !slice_differ && !pdiff);
	free(ws);
	free(ns);
	free(o);
	free(err);
	free(po);
	snprintf(cmd, sizeof cmd, "rm -rf %s", dir);
	CHECK(system(cmd) == 0);
	return 0;
}
