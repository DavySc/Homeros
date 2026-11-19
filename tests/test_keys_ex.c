/* The command line against Neovim on random keys (src/keys/ex_oracle.lua):
   ex ranges and :<line>, then / ? n N * # g* g# with offsets, as motions
   and with operators.  After each: the cursor, the error number and the
   text.  Marks a-z are not set by the keys yet, so 'a fails in both. */
#include <string.h>
#include <unistd.h>
#include "check.h"
#include "core.h"
#include "keys.h"
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
static const char *const AFTER[] = { "n", "N", "2n", "*", "#", "g*", "g#", "2*", ".", "dn", "yN", "n.", "x" };

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

/* n random cases of gen on text: how many differ from Neovim */
static int check(const char *text, void (*gen)(char *))
{
	char dir[] = "/tmp/test_keys_exXXXXXX", tf[64], cases[64], out[64], cmd[512], k[128];
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
		char err[16], want[512], got[512], of[80];
		CHECK(fgets(k, sizeof k, c));
		snprintf(of, sizeof of, "%s.%d", out, i + 1);
		FILE *o = fopen(of, "r");
		/* none: Neovim looped (a case that ends in a loop, such as a
		   recursive macro, which the generator does not make, measured) */
		int ok = o && fgets(want, sizeof want, o) && sscanf(want, "%ld %ld %15s", &line, &col, err) == 3 && fgets(want, sizeof want, o);
		if (o) fclose(o), unlink(of);
		if (!ok) { none++; continue; }
		want[strcspn(want, "\n")] = 0;
		static Editor e;
		CHECK(!core_open(&e, tf));
		normal_reset();
		/* v:errmsg: the last error, though later messages replace it */
		char m[sizeof e.msg] = "-";
		for (char *s = k; *s != '\n'; s++) {
			keys_feed(&e, *s == '\r' ? KEY_ENTER : *s == 27 ? KEY_ESC : (unsigned char)*s, s[1] != '\n');
			if (e.msg[0] == 'E') memcpy(m, e.msg, sizeof m);
		}
		size_t n = text_read(e.buf->text, 0, got, sizeof got - 1);
		got[n] = 0;
		if (n && got[n - 1] == '\n') got[n - 1] = 0;
		for (char *s = got; *s; s++) if (*s == '\n') *s = 1;
		if ((long)e.win->line + 1 != line || (long)e.win->col != col || strncmp(m, err, strlen(err)) || (m[0] == 'E' && m[strlen(err)] != ':')
		    || strcmp(got, want)) {
			k[strcspn(k, "\n")] = 0;
			for (char *s = k; *s; s++) if (*s == '\r' || *s == 27) *s = *s == '\r' ? '|' : '~';
			if (bad++ < 10) fprintf(stderr, "%d %s: line %zu col %zu [%s], Neovim %ld %ld %s%s\n", i, k, e.win->line + 1, e.win->col, e.msg, line, col, err, strcmp(got, want) ? " (text differs)" : "");
		}
		core_free(&e);
	}
	fclose(c);
	CHECK(none < N / 100);
	if (bad) fprintf(stderr, "%d of %d differ; cases in %s (| is Enter, ~ Esc)\n", bad, N, dir);
	else unlink(tf), unlink(cases), rmdir(dir);
	return bad;
}

int main(void)
{
	static const char TEXT[] = "abcdef\n  xy\nhello world\n\tq\nlast line\nz\n";
	int bad = check(TEXT, range);
	/* past 2^63 Neovim's line numbers wrap (":9223372036854775806+5" goes
	   to line 3, measured once); ours saturate, without overflow */
	char path[] = "/tmp/test_keys_exXXXXXX";
	int fd = mkstemp(path);
	CHECK(fd >= 0 && write(fd, TEXT, sizeof TEXT - 1) == sizeof TEXT - 1 && !close(fd));
	static Editor e;
	CHECK(!core_open(&e, path));
	for (const char *s = ":99999999999999999999+99999999999999999999+9999999999999999999\r"; *s; s++) keys_feed(&e, *s == '\r' ? KEY_ENTER : *s, s[1] != 0);
	CHECK(e.win->line == 5 && !e.msg[0]);
	core_free(&e);
	unlink(path);
	bad += check("foo bar\nabc Foo x\n  foo.bar(a) 12\nbaz?\n\nx/y?z foo\nfoofoo zz\nq .*[^$~/?\\\n", search);
	CHECK(!bad);
	printf("test_keys_ex: %d random ranges and %d random searches, each as Neovim (cursor, error, text)\n", N, N);
	return 0;
}
