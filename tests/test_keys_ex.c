/* The command line against Neovim on random keys (src/keys/ex_oracle.lua):
   ex ranges and :<line>, then / ? n N * # g* g# with offsets, as motions
   and with operators, then :s (the c flag answered), with :noh and CTRL-L among the keys after.
   After each: the cursor, the error number, the text and the cells
   'hlsearch' draws from the top line on.  Marks a-z are not set by the keys yet, so 'a fails in both. */
#include <string.h>
#include <unistd.h>
#include "check.h"
#include "core.h"
#include "fold.h"
#include "keys.h"
#include "loop.h"
#include "uni.h"
#include "../src/keys/k.h"

enum { N = 3000 };
static const char *const PRE[] = { "", "j", "jj", "G", "lll", "jllll", "jjj", "Vj\x1b", "jvjj\x1bgg", "3j", "Vj" };
static const char *const TOK[] = { "1", "2", "3", "0", "6", "99", ".", "$", "'<", "'>", "'a", "+", "-", "+2",
                                   "-3", "4", " ", ",", ";", "%", ":", "Z" };
/* searches: what comes before, the pattern's pieces, the offsets */
static const char *const SPRE[] = { "", "", "w", "j", "G$", "jjw", "2", "d", "y", "c", "v", "3d", "gU" };
static const char *const PAT[] = { "foo", "o", "a", "Foo", "x", "\\<", "\\>", ".", "^", "$", "\\d", "[ab]", "\\c",
                                   "o\\+", " ", "\\/", "\\?", "?", "/", "b.r", "\\(o\\)", "*", "zz" };
static const char *const OFF[] = { "", "", "", "/", "/e", "/s+1", "/b-1", "/e+1", "/+1", "/-1", "/2", "/e-2", "/s" };
/* Esc before a : (an ex command as a motion is not built) */
static const char *const AFTER[] = { "n", "N", "2n", "*", "#", "g*", "g#", "2*", ".", "dn", "yN", "n.", "x",
                                     "\x1b:noh\r", "\x0c", "zt", "jzt", "d\x0c", "v\x0c\x1b", "\x1b" "3:nohlsearch\r", "\x1b:nohlsearc\r" };

static void range(char *k)
{
	strcpy(k, PRE[rnd(sizeof PRE / sizeof *PRE)]);
	if (!rnd(4)) strcat(k, rnd(2) ? "2" : "3");
	/* Neovim's command line takes a ":" typed after "'<,'>" (Visual
	   mode's or typed, blanks between) differently from one after "1,2"
	   (measured, 3 runs; not understood yet): left out */
	int vis = k[0] == 'V' && k[2] != 27;
	strcat(k, ":");
	for (unsigned t = rnd(5); t--;) {
		const char *x = TOK[rnd(sizeof TOK / sizeof *TOK)];
		size_t l = strlen(k);
		while (k[l - 1] == ' ') l--;
		if (strcmp(x, ":") || (vis ? k[l - 1] != ':' : l < 5 || strncmp(k + l - 5, "'<,'>", 5))) strcat(k, x);
	}
	strcat(k, "\r");
}

static void search(char *k)
{
	strcpy(k, SPRE[rnd(sizeof SPRE / sizeof *SPRE)]);
	char d[2] = { rnd(3) ? '/' : '?', 0 };
	strcat(k, d);
	/* the delimiter unescaped would end the pattern there: the rest is
	   not an offset, and Neovim waits for a key after its error */
	for (unsigned t = 1 + rnd(3); t--;) {
		const char *x = PAT[rnd(sizeof PAT / sizeof *PAT)];
		if (strcmp(x, d)) strcat(k, x);
	}
	const char *o = OFF[rnd(sizeof OFF / sizeof *OFF)];
	if (*o) strcat(k, d), strcat(k, o + 1);   /* the offset after the delimiter */
	strcat(k, rnd(15) ? "\r" : "\x1b");
	if (k[0] == 'c' || strstr(k, "\x1b")) strcat(k, "X\x1b");
	if (k[0] == 'v') strcat(k, "d");
	for (unsigned t = rnd(3); t--;) strcat(k, AFTER[rnd(sizeof AFTER / sizeof *AFTER)]);
}

/* :s: what comes before, the range, the command, the pattern's and the
   replacement's pieces, the flags, and the keys after */
static const char *const XPRE[] = { "", "", "j", "G", "jjw", "Vj", "/o\r" };
static const char *const XRANGE[] = { "", "", "", "%", "%", ".", "2", "1,3", ".,$", "2;+1", "$", "0", "3,4" };
static const char *const XCMD[] = { "s", "s", "s", "s", "su", "substitute", "&", "&&", "~", "s" };
static const char *const XDELIM[] = { "/", "/", "/", "#", "+" };
static const char *const XPAT[] = { "foo", "o", "a", "Foo", "x", "\\<", "\\>", ".", "^", "$", "\\d", "[ab]", "\\c", "o\\+",
                                    " ", "\\(o\\)", "\\(.\\)\\(.\\)", "zz", "\\n", "\\_s", "\\zs", "o*", "\\_.", "b\\n\\zs", "?\\\\" };
static const char *const XSUB[] = { "", "x", "&", "\\0", "\\1", "\\2", "~", "\\r", "\\u", "\\U", "\\L", "\\l", "\\e", "\\&",
                                    "\\\\", "é", "\\t", "-", "\\n", "AB", "\\é", "e\xcc\x81" };
static const char *const XFLAGS[] = { "", "", "", "g", "g", "gi", "I", "e", "n", "&", "r", "ge", " 2", "g 3", "x", "gn", "i" };
static const char *const XAFTER[] = { "n", "N", "u", "&", "g&", "x", "u", ".", "2&", "\x1b:noh\r", "\x0c", "zt", "jzt" };

/* cases the random ones reached only with other seeds: a NUL in a line,
   :s/\n// on the last line, a NUL after the indent */
static const char *const XFIXED[] = { "/o\r:s+\\c+&\\n+gi\r.&", ":$s+\\n+\r", "G:3,4su/ \\zs/\\n/\r" };

static void substitute(char *k)
{
	static unsigned fixed;
	if (fixed < sizeof XFIXED / sizeof *XFIXED) { strcpy(k, XFIXED[fixed++]); return; }
	strcpy(k, XPRE[rnd(sizeof XPRE / sizeof *XPRE)]);
	strcat(k, ":");
	/* after Visual mode's '<,'> a range is taken differently (range()) */
	if (k[0] != 'V') strcat(k, XRANGE[rnd(sizeof XRANGE / sizeof *XRANGE)]);
	const char *c = XCMD[rnd(sizeof XCMD / sizeof *XCMD)];
	strcat(k, c);
	/* letters after :s are its name (:sg is :s g, :se is :set): a blank */
	if (c[0] == 's' && !rnd(6)) strcat(k, " ");
	else if (c[0] == 's') {
		const char *d = XDELIM[rnd(sizeof XDELIM / sizeof *XDELIM)];
		strcat(k, d);
		for (unsigned t = rnd(3); t--;) strcat(k, XPAT[rnd(sizeof XPAT / sizeof *XPAT)]);
		strcat(k, d);
		for (unsigned t = rnd(3); t--;) strcat(k, XSUB[rnd(sizeof XSUB / sizeof *XSUB)]);
		if (rnd(4)) strcat(k, d);
	}
	strcat(k, XFLAGS[rnd(sizeof XFLAGS / sizeof *XFLAGS)]);
	strcat(k, "\r");
	for (unsigned t = rnd(3); t--;) strcat(k, XAFTER[rnd(sizeof XAFTER / sizeof *XAFTER)]);
}

/* :s with the c flag, a pattern that matches on every range so that it
   asks (after an error the answers would be normal keys), its answers,
   Esc for a question still open (Neovim would wait for a key), and keys
   after, which & g& and . may make ask again.  a, q and l come last: past
   the question a starts Insert mode and q a recording. */
static const char *const CRANGE[] = { "", "%", "%", "1,3", ".,$", "2;+1", "10,25" };
static const char *const CPAT[] = { "o", ".", "\\<", "^", "$", "o*", "\\_s", "\\n", "\\_.", "\\zs", "foo", "\\(o\\)" };
static const char *const CFLAGS[] = { "c", "gc", "gc", "gci", "gce" };
static const char *const CANSWER[] = { "y", "y", "n", "n", "\x05", "\x05", "\x19", "x", "\x1b", "\x03" };
static const char *const CLAST[] = { "", "a", "q", "l" };

static void confirm(char *k)
{
	strcpy(k, XPRE[rnd(sizeof XPRE / sizeof *XPRE)]);
	strcat(k, ":");
	if (k[0] != 'V') strcat(k, CRANGE[rnd(sizeof CRANGE / sizeof *CRANGE)]);
	strcat(k, "s/");
	strcat(k, CPAT[rnd(sizeof CPAT / sizeof *CPAT)]);
	strcat(k, "/");
	for (unsigned t = rnd(3); t--;) strcat(k, XSUB[rnd(sizeof XSUB / sizeof *XSUB)]);
	strcat(k, "/");
	strcat(k, CFLAGS[rnd(sizeof CFLAGS / sizeof *CFLAGS)]);
	strcat(k, "\r");
	for (unsigned t = 1 + rnd(5); t--;) strcat(k, CANSWER[rnd(sizeof CANSWER / sizeof *CANSWER)]);
	strcat(k, CLAST[rnd(sizeof CLAST / sizeof *CLAST)]);
	strcat(k, "\x1b\x1b");
	for (unsigned t = rnd(3); t--;) strcat(k, XAFTER[rnd(sizeof XAFTER / sizeof *XAFTER)]);
	strcat(k, "\x1b\x1b");
}

/* marks and the jump list: marks set, jumps (each sets '' and joins the
   jump list), the marks gone to with ' ` g' g` and as motions, CTRL-O and
   Tab, among edits that move the marks; '[ and '] right after what sets
   them.  Not yet: '[ '] before that and '. (the core sets them on a read
   and a change), A-Z, an undo's jump, an operator
   whose jump deletes the line of '' (w_prev_pcmark), and J, which moves ''
   (mark_col_adjust()): they wait for the core (package 4) */
static const char *const MPRE[] = { "", "j", "G", "3j", "w", "jjw", "Vj\x1b", "v3l\x1b", "m'" };
static const char *const MTOK[] = { "ma", "mb", "m'", "m`", "m<", "m>", "m\"", "mz", "'a", "`a", "'b", "`b", "''", "``", "'`",
                                    "`'", "'<", "`>", "'\"", "`^", "'^", "g'a", "g`b", "g''", "g``", "'{", "`}", "'(", "`)",
                                    "'z", "'x", "'A", "'1", "'!", "y'a", "y`b", "gu'a", "gU`a", "\x0f", "\x0f", "\t", "2\x0f",
                                    "3\t", "\x0f\x0f", "G", "gg", "3G", "%", "}", "{", ")", "(", "*", "#", "n", "N", "H",
                                    "M", "L", "/o\r", "?a\r", ":3\r", ":s/o/0/\r", ":'a,'bs/^/-/\r", "[(", "])",
                                    "x", "O\x1b", "oxy\x1b", "ix\x1b", "Ay\x1b", "yyP", "p", "i\r\x1b", "r\r", "j", "k",
                                    "w", "b", "$", "0", "l", "yj`]", "yw`[", "cwX\x1b`]", "gUiw']", "x`[", "yi(", "va(\x1b", "50%0", "2G$%",
                                    "~`[", "~`]", "rx`[", "3rx`]", "dd", "dj" };

/* "50%0": N% keeps the wanted column, which Neovim under -l has as the
   column after j (measured: "Ay Esc j 50%" ends in column 7 there, 12 in
   nvim --headless with a file); the 0 leaves that aside */
/* first these, each once: what random sequences reach too rarely (PR 116's
   review): an undo's '[ and one inside Insert, '] of an exclusive yank from column 0,
   ~ and r, gv with one end or none, a bare ',
   '{ '( at the ends, m[ m], Enter at column 0 of an indented line, [( and ]) as jumps */
static const char *const MFIX[] = { "Gicd\x0fuef\x1b`]", "jvjy`]", "jy0`]", "jjyl`]", "yjrx`]", "jm<gvd", "jm>gvd", "m<jm>gvd",
                                    ":'\r", ":'\rx", "'{", "`(", "G'}", "G`)", "jwm[gg`[", "jjm]gg`]", "jwma0i\r\x1b`a", "jwma0li\r\x1b`a", "jjixy\x1bggyyu`[", "Ggg7jdd\x0f", "Ggg6jdj\x0f",
                                    "j6l[(``", "j6l])``" };
static void marks(char *k)
{
	static unsigned fixed;
	if (fixed < sizeof MFIX / sizeof *MFIX) { strcpy(k, MFIX[fixed++]); return; }
	strcpy(k, MPRE[rnd(sizeof MPRE / sizeof *MPRE)]);
	for (unsigned t = 1 + rnd(8); t--;) strcat(k, MTOK[rnd(sizeof MTOK / sizeof *MTOK)]);
}

/* the cells 'hlsearch' draws, as ex_oracle.lua writes them: the core's
   use of keys_hl() (include/keys.h).  Drawn five times: after the first,
   only the line copies may grow (src/keys/buf.c, slot()), toward the
   longest line each has held */
static unsigned long allocs;
static void on_malloc(const volatile void *p, size_t n) { (void)p, (void)n, allocs++; }
static void on_free(const volatile void *p) { (void)p; }
int __sanitizer_install_malloc_and_free_hooks(void (*)(const volatile void *, size_t), void (*)(const volatile void *));
size_t __sanitizer_get_current_allocated_bytes(void);

static void hlsearch(Editor *e, char *b, size_t cap)
{
	E = e;
	unsigned long a = 0;
	for (int pass = 0; pass < 5; pass++) {
		a = pass == 1 ? allocs : a;
		size_t n = (size_t)snprintf(b, cap, "%zu", e->win->top);
		for (size_t l = e->win->top; l < core_lines(e) && l < e->win->top + (size_t)e->win->h; l++) {
			/* Neovim's window: 80 cells, 'nowrap' */
			long len = ml_len((long)l), w = vcol_of((long)l, len) < 79 ? vcol_of((long)l, len) : 79;
			b[n++] = 1;
			memset(b + n, '.', (size_t)w + 1);
			size_t s, z;
			for (size_t c = 0; c <= (size_t)len && keys_hl(e, e->win, l, c, &s, &z);) {
				/* an empty match: one character */
				size_t to = z > s ? z : s + (size_t)clen(ml_get((long)l), len, (long)s);
				for (long j = (long)s; j < (long)to && j <= len; j += clen(ml_get((long)l), len, j))
					for (long v = vcol_of((long)l, j); v <= vcol_end((long)l, j) && v <= w; v++) b[n + v] = '#';
				c = to;
			}
			n += (size_t)w + 1;
		}
		b[n] = 0;
		CHECK(n < cap && (pass < 1 || allocs - a <= 3));
	}
}

/* n random cases of gen on text: how many differ from Neovim */
/* folds: the z keys, moves over closed folds, linewise operators and
   Visual taking them whole, and what opens them ('foldopen'); an edit
   only last, since the core updates folds after edits (package 4); no
   Visual * (Neovim's default mapping, not built yet) */
static const char *const FTOK[] = { "zo", "zc", "za", "zR", "zM", "2zc", "2zo", "2za", "j", "k", "2j", "3k", "gg", "G", "3G",
                                    "yy", "yj", "yk", "y2j", "l", "h", "w", "b", "$", "0", "e", "fz", "%", "{", "}", "''",
                                    "ma", "'a", "`a", "\x0f", "/z\r", "n", "N", "V", "v", "\x1b", "Vjy", "vjy",
                                    "Vy", "u", "jzc", "kzc", "4Gzc", "9Gzc" };
/* Esc first: a Visual left on would make the edit a Visual one */
static const char *const FEND[] = { "", "\033dd", "\033dj", "\033dk", "\033x", "\033o\033", "\033O\033", "\033ix\033", "\033Vd", "\033p", "\033J" };
static void foldkeys(char *k)
{
	strcpy(k, "m'");   /* the '' mark Neovim's startup sets (ex_oracle.lua) */
	for (unsigned t = 1 + rnd(8); t--;) strcat(k, FTOK[rnd(sizeof FTOK / sizeof *FTOK)]);
	strcat(k, FEND[rnd(sizeof FEND / sizeof *FEND)]);
}

/* the fold phase's foldexpr, one value a line as Neovim's ("0", ">1"),
   given to the oracle as $FOLDS; NULL: no folds */
static const char *folds;
static int fold_expr(void *ctx, size_t line, int *start)
{
	(void)ctx;
	const char *v = folds;
	for (size_t i = 0; i < line && v; i++) v = strchr(v, ','), v = v ? v + 1 : NULL;
	if (!v) return 0;
	*start = *v == '>';
	return atoi(v + *start);
}
static int check(const char *text, void (*gen)(char *))
{
	char dir[] = "/tmp/test_keys_exXXXXXX", tf[64], cases[64], out[64], cmd[512], k[256];
	CHECK(mkdtemp(dir));
	snprintf(tf, sizeof tf, "%s/text", dir);
	snprintf(cases, sizeof cases, "%s/cases", dir);
	snprintf(out, sizeof out, "%s/out", dir);
	FILE *f = fopen(tf, "w");
	CHECK(f && fputs(text, f) >= 0 && !fclose(f));
	CHECK((f = fopen(cases, "w")));
	for (int i = 0; i < N; i++) gen(k), fprintf(f, "%s\n", k);
	CHECK(!fclose(f));
	/* one Neovim a case, four at a time (src/keys/ex_oracle.lua); % is
	   Neovim's own, as tools/conform.py's */
	snprintf(cmd, sizeof cmd, "seq %d | FOLDS='%s' xargs -P 4 -I{} timeout 10 nvim --headless --clean --cmd \"let g:loaded_matchit=1\" %s%s %s %s.{} {}%s 2>/dev/null",
	         N, folds ? folds : "", folds ? "-c \"lua _G.arg = vim.split('" : "-l src/keys/ex_oracle.lua ", tf, cases, out,
	         folds ? "', ' ')\" -c \"luafile src/keys/ex_oracle.lua\"" : "");
	CHECK(system(cmd) >= 0);
	FILE *c = fopen(cases, "r");
	CHECK(c);
	int bad = 0, none = 0;
	for (int i = 0; i < N; i++) {
		long line, col;
		char err[16], want[8192], got[8192], of[80], hw[16384], hg[16384];
		CHECK(fgets(k, sizeof k, c));
		snprintf(of, sizeof of, "%s.%d", out, i + 1);
		FILE *o = fopen(of, "r");
		/* none: Neovim looped (a case that ends in a loop, such as a
		   recursive macro, which the generator does not make, measured) */
		int ok = o && fgets(want, sizeof want, o) && sscanf(want, "%ld %ld %15s", &line, &col, err) == 3 && fgets(want, sizeof want, o)
		         && fgets(hw, sizeof hw, o);
		if (o) fclose(o), unlink(of);
		if (!ok) { none++; continue; }
		want[strcspn(want, "\n")] = 0;
		static Editor e;
		CHECK(!core_open(&e, tf));
		if (folds) {
			CHECK((e.win->fold = fold_new(99, fold_expr, NULL, NULL)));
			fold_update(e.win->fold, 0, core_lines(&e), core_lines(&e));
		}
		normal_reset();
		ops_reset(), edit_reset(), motion_reset();   /* each case is a fresh Neovim (a recording, registers) */
		/* v:errmsg: the last error, though later messages replace it */
		char m[sizeof e.msg] = "-";
		/* keys are code points (é is one key) */
		for (size_t j = 0, n = strlen(k), l; k[j] != '\n'; j += l) {
			l = uni_len((const unsigned char *)k, n, j);
			int c = (int)uni_cp((const unsigned char *)k, n, j);
			keys_feed(&e, c == '\r' ? KEY_ENTER : c == 27 ? KEY_ESC : c, k[j + l] != '\n');
			if (e.msg[0] == 'E') memcpy(m, e.msg, sizeof m);
		}
		size_t n = text_read(e.buf->text, 0, got, sizeof got - 1);
		if (n && got[n - 1] == '\n') n--;
		got[n] = 0;
		/* a NUL in a line (\n in a replacement) as ex_oracle.lua writes it */
		for (size_t j = 0; j < n; j++) got[j] = got[j] == '\n' ? 1 : got[j] ? got[j] : 2;
		hw[strcspn(hw, "\n")] = 0;
		hlsearch(&e, hg, sizeof hg);
		if ((long)e.win->line + 1 != line || (long)e.win->col != col || strncmp(m, err, strlen(err)) || (m[0] == 'E' && m[strlen(err)] != ':')
		    || strcmp(got, want) || (!strchr(hw, '?') && strcmp(hg, hw))) {
			k[strcspn(k, "\n")] = 0;
			for (char *s = k; *s; s++) if (*s == '\r' || *s == 27) *s = *s == '\r' ? '|' : '~';
			if (bad++ < 10) fprintf(stderr, "%d %s: line %zu col %zu [%s], Neovim %ld %ld %s%s%s\n", i, k, e.win->line + 1, e.win->col, e.msg, line, col, err, strcmp(got, want) ? " (text differs)" : "",
				                      !strchr(hw, '?') && strcmp(hg, hw) ? " (hlsearch differs)" : "");
			if (bad <= 10 && !strchr(hw, '?') && strcmp(hg, hw)) fprintf(stderr, "  %s\n  Neovim %s\n", hg, hw);
		}
		if (folds) fold_free(e.win->fold), e.win->fold = NULL;   /* the core's to free once it makes them */
		core_free(&e);
	}
	fclose(c);
	CHECK(none < N / 100);
	if (bad) fprintf(stderr, "%d of %d differ; cases in %s (| is Enter, ~ Esc)\n", bad, N, dir);
	else unlink(tf), unlink(cases), rmdir(dir);
	return bad;
}

static void feed(Editor *e, const char *s)
{
	for (; *s; s++) keys_feed(e, *s == '\r' ? KEY_ENTER : *s, s[1] != 0);
}

/* a file holding n bytes of s repeated, then t */
static void text(const char *path, const char *s, size_t n, const char *t)
{
	FILE *f = fopen(path, "w");
	CHECK(f);
	for (size_t i = 0; i < n; i++) fputc(s[i % strlen(s)], f);
	CHECK(fputs(t, f) >= 0 && !fclose(f));
}

int main(void)
{
	if (getenv("SEED")) rng += strtoull(getenv("SEED"), NULL, 10);
	CHECK(__sanitizer_install_malloc_and_free_hooks(on_malloc, on_free));
	static const char TEXT[] = "abcdef\n  xy\nhello world\n\tq\nlast line\nz\n";
	CHECK(!check(TEXT, range));   /* each phase stops the test, so a caught plant does not run the rest */
	CHECK(!check("abc. def foo\n  xy (o)\n\nhello world. Two\n\tq a\nlast line\n\n  z o\n", marks));
	folds = "0,>1,1,>2,2,2,1,0,>1,1";
	CHECK(!check("a x\nb {\n c y\n d {\n  e z\n  f\n g z\n}\ni {\n j\n", foldkeys));
	folds = NULL;
	/* past 2^63 Neovim's line numbers wrap (":9223372036854775806+5" goes
	   to line 3, measured once); ours saturate, without overflow */
	char path[] = "/tmp/test_keys_exXXXXXX";
	int fd = mkstemp(path);
	CHECK(fd >= 0 && write(fd, TEXT, sizeof TEXT - 1) == sizeof TEXT - 1 && !close(fd));
	static Editor e;
	CHECK(!core_open(&e, path));
	feed(&e, ":99999999999999999999+99999999999999999999+9999999999999999999\r");
	CHECK(e.win->line == 5 && !e.msg[0]);
	/* 'hlsearch' after the text changed without a key (a reload, a format) */
	size_t hs, hz;
	feed(&e, "/xy\r");
	CHECK(keys_hl(&e, e.win, 1, 0, &hs, &hz) && hs == 2 && hz == 4 && !core_edit(&e, 7, 0, "ab", 2));
	CHECK(keys_hl(&e, e.win, 1, 0, &hs, &hz) && hs == 4 && hz == 6);
	/* the c flag's question, which the random cases cannot see (Neovim
	   waits for its key): the line shown with the substitutions so far,
	   the cursor moved by their length again at each other key, the match
	   in IncSearch, keys held back from maps; a redraw's 'hlsearch' ("O"
	   without ignorecase) between the keys; one undo step */
	size_t n;
	for (const char *s = ":3s/O/XY/gci\ry"; *s; s++) {
		keys_feed(&e, *s == '\r' ? KEY_ENTER : *s, s[1] != 0);
		CHECK(!keys_hl(&e, e.win, 2, 0, &hs, &hz));
	}
	const unsigned char *l = core_line(&e, 2, &n);
	CHECK(n == 12 && !memcmp(l, "hellXY world", 12) && e.win->col == 8 && e.ks.pending);
	CHECK(e.ks.inc.type == 'v' && e.ks.inc.l1 == 2 && e.ks.inc.c1 == 8 && e.ks.inc.l2 == 2 && e.ks.inc.c2 == 8);
	keys_feed(&e, 'z', 0);
	CHECK(e.win->col == 9 && e.ks.inc.c1 == 9);
	keys_feed(&e, KEY_ESC, 0);
	l = core_line(&e, 2, &n);
	CHECK(!e.ks.pending && !e.ks.inc.type && n == 12 && !memcmp(l, "hellXY world", 12));
	keys_feed(&e, 'u', 0);
	l = core_line(&e, 2, &n);
	CHECK(n == 11 && !memcmp(l, "hello world", 11));
	/* a match that starts above the window is not drawn
	   (prepare_search_hl(); measured, nvim --headless screenattr()) */
	feed(&e, "/d\\n\\s*q\\nl\r");
	e.win->top = 2;
	CHECK(keys_hl(&e, e.win, 4, 0, &hs, &hz) && hs == 0 && hz == 1);
	e.win->top = 3;
	CHECK(!keys_hl(&e, e.win, 4, 0, &hs, &hz));
	/* one that \zs starts on the next line is that line's */
	feed(&e, "/xy\\n\\zshe\r");
	e.win->top = 0;
	CHECK(!keys_hl(&e, e.win, 1, 0, &hs, &hz) && keys_hl(&e, e.win, 2, 0, &hs, &hz) && hs == 0 && hz == 2);
	/* the question ends when another file comes into the window, not by a
	   key, or when the window closes: its answer goes nowhere */
	char other[] = "/tmp/test_keys_exXXXXXX";
	fd = mkstemp(other);
	CHECK(fd >= 0 && write(fd, "xo\nxo\nxo\n", 9) == 9 && !close(fd));
	feed(&e, ":3s/o/0/gc\r");
	CHECK(e.ks.pending && !core_edit_file(&e, other, 0));
	feed(&e, "y");
	l = core_line(&e, 2, &n);
	CHECK(!e.ks.pending && n == 2 && !memcmp(l, "xo", 2) && !core_edit_file(&e, path, 0));
	CHECK(!core_split(&e, 0, 0));
	feed(&e, ":3s/o/0/gc\r");
	keys_feed(&e, KEY_CLOSE, 0);
	CHECK(!e.ks.pending && !strstr(e.msg, "replace with"));
	feed(&e, "y");
	l = core_line(&e, 2, &n);
	CHECK(n == 11 && !memcmp(l, "hello world", 11));
	core_free(&e);
	unlink(path), unlink(other);
	/* CTRL-E at the question with the window at the file's end: the
	   cursor stays 'scrolloff' rows from the top (Neovim: top 39 after
	   three, line 39 at the top, measured once, reviews/pr-90-repro/ctrl-e.sh) */
	char t60[1024] = "";
	for (int i = 1; i <= 60; i++) snprintf(t60 + strlen(t60), sizeof t60 - strlen(t60), "%d foo\n", i);
	text(path, "", 0, t60);
	CHECK(!core_open(&e, path));
	/* drawn after each key, as the editor does (the core looks at the
	   view again when it draws) */
	static Grid g;
	CHECK(!grid_resize(&g, 24, 80));
	keys_feed(&e, KEY_ESC, 0);   /* the y above waits for its motion */
	for (const char *k = "G:45,60s/foo/x/c\r\x05\x05\x05"; *k; k++) keys_feed(&e, *k == '\r' ? KEY_ENTER : *k, 0), core_draw(&e, &g);
	CHECK(e.win->line == 44 && e.win->top == 38);
	keys_feed(&e, 'q', 0), core_draw(&e, &g);
	CHECK(e.win->top == 38);
	grid_free(&g);
	core_free(&e);
	/* a repeat as long as the text grows no C stack (one call per pass
	   of a group or per character of \_. crashed past 9 KB): a line of
	   100,000 bytes, 300,000 bytes of lines */
	text(path, "ab", 100000, "\n");
	CHECK(!core_open(&e, path));
	feed(&e, "/\\(a\\|b\\)*$\r");
	/* as Neovim on 60,000 bytes (it wraps to the match at the cursor; at
	   100,000 it says E486, measured once) */
	CHECK(e.win->line == 0 && e.win->col == 0 && !strcmp(e.msg, "search hit BOTTOM, continuing at TOP"));
	core_free(&e);
	/* the stacks a* grows on 4 MB of a are not kept: 7.8 MB stays, not
	   108 MB (measured once each; E486, as Neovim) */
	text(path, "a", 4000000, "\n");
	CHECK(!core_open(&e, path));
	size_t held = __sanitizer_get_current_allocated_bytes();
	feed(&e, "/a*b\r");
	CHECK(!strncmp(e.msg, "E486", 4) && __sanitizer_get_current_allocated_bytes() < held + (32 << 20));
	core_free(&e);
	/* 'redrawtime': a frame of a pattern that takes long on every line ends
	   after 2 s.  Each line here gives up after MAXSTEPS: without the budget
	   50 lines took about 3 s under ASan, so a fast runner passed; 400 take
	   28 to 35 s, with it 2.02 to 2.88 s, the most with 64 busy processes
	   on 4 cores (measured, 3 to 5 runs each): 8 s lies between */
	text(path, "aaaaaaaaaaaaaaaa\n", 17 * 400, "b\n");
	CHECK(!core_open(&e, path));
	feed(&e, "400G/\\(a*\\)*b\r");
	CHECK(e.win->line == 400);
	e.win->top = 0;
	long long t0 = loop_now();
	for (size_t i = 0; i < 400; i++) CHECK(!keys_hl(&e, e.win, i, 0, &hs, &hz));
	CHECK(loop_now() - t0 < 8000000000LL);
	/* Ctrl-C (main.c sets loop_int) stops a search and a :s at the next
	   line, as Neovim's line_breakcheck(): "Interrupted", nothing changed */
	loop_int = 1;
	/* drawing has its own budget: it finds the match and leaves Ctrl-C to the search */
	CHECK(keys_hl(&e, e.win, 400, 0, &hs, &hz) && loop_int);
	feed(&e, "gg/b\r");
	CHECK(e.win->line == 0 && !strcmp(e.msg, "Interrupted"));
	feed(&e, ":%s/a/x/\r");
	CHECK(!strcmp(e.msg, "Interrupted") && *core_line(&e, 0, &(size_t){ 0 }) == 'a');
	loop_int = 0;
	feed(&e, "/b\r");
	CHECK(e.win->line == 400);
	/* the oracle's set_lines() fills the change list; a file just read has
	   none (nvim --headless on a file: E664, measured once) */
	feed(&e, "g;");
	CHECK(!strcmp(e.msg, "E664: Changelist is empty"));
	core_free(&e);
	/* '[ and '] are each buffer's: a yank in one sets none in another,
	   and they are there when the first comes back (PR 116's review,
	   twobuf.vim: Neovim has B's own from its read, which the core does
	   not set yet, so here B's is unset) */
	char a[] = "/tmp/keys_exAXXXXXX", b2[] = "/tmp/keys_exBXXXXXX";
	int fa = mkstemp(a), fb = mkstemp(b2);
	CHECK(write(fa, "a1\na2\na3\na4\n", 12) == 12 && !close(fa) && write(fb, "b1\nb2\nb3\nb4\n", 12) == 12 && !close(fb));
	CHECK(!core_open(&e, a));
	feed(&e, "jjyy");
	CHECK(!core_edit_file(&e, b2, 0));
	feed(&e, "`[");
	CHECK(!strncmp(e.msg, "E20", 3));
	feed(&e, "ggx");
	CHECK(!core_edit_file(&e, a, 1));
	feed(&e, "G`[");
	CHECK(e.win->line == 2);
	core_free(&e);
	/* and when the key itself goes to the other buffer (two windows,
	   CTRL-W w; :q and :Dashboard alike) */
	CHECK(!core_open(&e, a));
	feed(&e, "\x17s");
	CHECK(!core_edit_file(&e, b2, 0));
	feed(&e, "x\x17wjjyy\x17wG`[");
	CHECK(e.win->line == 0);
	feed(&e, "\x17wG`[");
	CHECK(e.win->line == 2);
	unlink(a), unlink(b2);
	core_free(&e);
	text(path, "select a from t;\n", 300000, "END\n");
	CHECK(!core_open(&e, path));
	feed(&e, "G/select\\_.\\{-}END\r");
	CHECK(e.win->line == 0 && e.win->col == 0 && !strcmp(e.msg, "search hit BOTTOM, continuing at TOP"));
	core_free(&e);
	unlink(path);
	CHECK(!check("foo bar\nabc Foo x\n  foo.bar(a) 12\nbaz?\n\nx/y?z foo\nfoofoo zz\nq .*[^$~/?\\\n", search));
	CHECK(!check("foo bar\nabc Foo x\n  foo.bar(a) 12\nbaz?\\\n\nx/y?z foo\nfoofoo zz\n", substitute));
	/* four times as long as the window's 22 lines can show: CTRL-E and
	   CTRL-Y scroll */
	char t4[256] = "";
	for (int i = 0; i < 4; i++) strcat(t4, "foo bar\nabc Foo x\n  foo.bar(a) 12\nbaz?\\\n\nx/y?z foo\nfoofoo zz\n");
	CHECK(!check(t4, confirm));
	printf("test_keys_ex: %d random ranges, %d mark and jump sequences, %d searches, %d substitutions, %d answered and %d over folds, each as Neovim (cursor, error, text, 'hlsearch')\n", N, N, N, N, N, N);
	return 0;
}
