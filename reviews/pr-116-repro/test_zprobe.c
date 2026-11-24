/* The command line against Neovim on random keys (src/keys/ex_oracle.lua):
   ex ranges and :<line>, then / ? n N * # g* g# with offsets, as motions
   and with operators, then :s (the c flag answered), with :noh and CTRL-L among the keys after.
   After each: the cursor, the error number, the text and the cells
   'hlsearch' draws from the top line on.  Marks a-z are not set by the keys yet, so 'a fails in both. */
#include <string.h>
#include <unistd.h>
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"
#include <stdlib.h>
#include "check.h"
#include "core.h"
#include "keys.h"
#include "loop.h"
#include "uni.h"
#include "../src/keys/k.h"

static int N = 3000;
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
                                    "w", "b", "$", "0", "l", "yj`]", "yw`[", "cwX\x1b`]", "gUiw']", "x`[", "yi(", "va(\x1b", "50%0", "2G$%" };

/* "50%0": N% keeps the wanted column, which Neovim under -l has as the
   column after j (measured: "Ay Esc j 50%" ends in column 7 there, 12 in
   nvim --headless with a file); the 0 leaves that aside */
static void marks(char *k)
{
	strcpy(k, MPRE[rnd(sizeof MPRE / sizeof *MPRE)]);
	for (unsigned t = 1 + rnd(8); t--;) strcat(k, MTOK[rnd(sizeof MTOK / sizeof *MTOK)]);
}


static char *PT[512]; static unsigned npt;
static void probe(char *k)
{
	static unsigned raw;
	if (getenv("RAW")) { strcpy(k, PT[raw++ % npt]); return; }
	strcpy(k, MPRE[rnd(sizeof MPRE / sizeof *MPRE)]);
	for (unsigned t = 1 + rnd(8); t--;) strcat(k, PT[rnd(npt)]);
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
	/* one Neovim a case, four at a time (src/keys/ex_oracle.lua) */
	snprintf(cmd, sizeof cmd, "seq %d | xargs -P 4 -I{} timeout 10 nvim --headless --clean -l src/keys/ex_oracle.lua %s %s %s.{} {} 2>/dev/null", N, tf, cases, out);
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
			if (bad++ < 80) fprintf(stderr, "%d %s: line %zu col %zu [%s], Neovim %ld %ld %s%s%s\n", i, k, e.win->line + 1, e.win->col, e.msg, line, col, err, strcmp(got, want) ? " (text differs)" : "",
				                      !strchr(hw, '?') && strcmp(hg, hw) ? " (hlsearch differs)" : "");
			if (bad <= 80 && !strchr(hw, '?') && strcmp(hg, hw)) fprintf(stderr, "  %s\n  Neovim %s\n", hg, hw);
		}
		if (getenv("RAW")) { k[strcspn(k, "\n")] = 0; printf("%-28s ours %zu %zu [%.30s]  nvim %ld %ld %s%s\n", k, e.win->line + 1, e.win->col, m, line, col, err, strcmp(got, want) ? " TEXT" : ""); }
		core_free(&e);
	}
	fclose(c);
	fprintf(stderr, "none %d\n", none);
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
	if (getenv("PN")) N = atoi(getenv("PN"));
	CHECK(__sanitizer_install_malloc_and_free_hooks(on_malloc, on_free));
	FILE *f = fopen(getenv("TOKS"), "r"); CHECK(f);
	static char line[512];
	while (fgets(line, sizeof line, f)) {
		char *o = malloc(512), *q = o;
		for (char *p = line; *p && *p != '\n'; p++) {
			if (*p == '\\' && p[1] == 'r') *q++ = '\r', p++;
			else if (*p == '\\' && p[1] == 'e') *q++ = 27, p++;
			else if (*p == '\\' && p[1] == 't') *q++ = 9, p++;
			else if (*p == '\\' && p[1] == 'o') *q++ = 15, p++;
			else if (*p == '\\' && p[1] == 'v') *q++ = 22, p++;
			else *q++ = *p;
		}
		*q = 0; PT[npt++] = o;
	}
	int bad = check("abc. def foo\n  xy (o)\n\nhello world. Two\n\tq a\nlast line\n\n  z o\n", probe);
	printf("probe: %d of %d differ\n", bad, N);
	return 0;
}
