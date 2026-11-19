/* Vim's patterns (src/keys/regex.c) against Neovim on random patterns and
   lines (src/keys/regex_oracle.lua): every match substitute(text, pat,
   '[&]', 'g') finds, or that the pattern is an error, as either of Neovim's
   engines gives it; SEED picks other cases. */
#include <string.h>
#include <unistd.h>
#include "check.h"
#include "core.h"
#include "keys.h"
#include "uni.h"
#include "../src/keys/k.h"

enum { N = 10000 };
static const char *const TEXT[] = { "a", "b", "A", "B", " ", "\t", "x", ".", "_", "-", "1", "é", "e\xcc\x81", "\xc3\x9f", "*", "(" };
static const char *const LIT[] = { "a", "b", "A", "x", " ", ".", "\\.", "_", "-", "1", "é", "e", "\\t", "~", "\\%d97", "\\%x41", "[", "\\[",
                                   "\\/", "*", "{", "\\{", "\\}", "}", "\\\\", "/", "%", "\\e", "=", "?", "+", "@", "<", ">", "(", ")", "|", "&", "\\*", "\\~" };
static const char *const CLS[] = { ".", "\\s", "\\S", "\\w", "\\W", "\\a", "\\l", "\\u", "\\d", "\\D", "\\k", "\\i", "\\h", "\\x", "\\p",
                                   "[ab]", "[^a ]", "[a-c]", "[[:upper:]]", "[[:alpha:]x]", "[]a]", "[-x]", "[^]]", "[a\\-]", "[\\t ]",
                                   "[[:lower:][:digit:]]", "[é-ê]", "[A-Z]" };
static const char *const MULTI[] = { "*", "\\+", "\\=", "\\?", "\\{2}", "\\{1,2}", "\\{-}", "\\{-1,}", "\\{,1}", "\\{}", "\\{-,2}", "\\{3,1}" };
static const char *const POS[] = { "^", "$", "\\<", "\\>", "\\zs", "\\ze", "\\c", "\\C", "\\v", "\\V", "\\m", "\\M", "\\1", "\\2" };
/* Neovim's engines disagree with each other and with the rest of their
   own rules where a literal e meets e + U+0301 (\(e\|\)\. there: the NFA
   engine finds no match, the backtracking one matches e alone), so these
   cases are known differences (src/keys/README) */
static const char *const KNOWN[][2] = {
	{ "\\(A\\{-,2}\\([^]]\\{-,2}e\\{}\\p\\{-,2}\\|\\&\\)\\)\\@>\\|+", "x\xc3\xa9_xe\xcc\x81" "e\xcc\x81" "x" },
};
/* the NFA engine's answers, which Neovim uses unless a pattern needs the
   other: the first cases, compared with it alone */
static const char *const FIXED[][2] = {
	{ "\\W\\@<=", "e\xcc\x81" "x.y" },                 /* a look-behind steps back over the accent too */
	{ "\\%(\\zsx\\)\\@<=\\.", "e\xcc\x81" "x.y" },     /* \zs in it does nothing */
	{ "\\%(b\\ze\\)\\@=x\\|bc", "abcd" },                /* \ze in a look-ahead is undone when what follows fails */
	{ "e\\{3,1}\\k", "xe\xcc\x81" },                     /* a repeat takes e from e + U+0301 */
	{ "e*", "e\xcc\x81" "_e" },                             /* but a match does not end before U+0301 */
};
enum { NF = sizeof FIXED / sizeof *FIXED };
static char pat[256];

static void add(const char *s) { if (strlen(pat) + strlen(s) < sizeof pat - 1) strcat(pat, s); }
#define PICK(a) a[rnd(sizeof a / sizeof *a)]

static void gen(int depth)
{
	for (unsigned n = 1 + rnd(4); n--;) {
		unsigned k = rnd(20);
		if (k < 7) add(PICK(LIT));
		else if (k < 11) add(PICK(CLS));
		else if (k < 13) add(PICK(POS));
		else if (k < 16 && depth < 3) {
			add(rnd(4) ? "\\(" : "\\%(");
			gen(depth + 1);
			if (rnd(3) == 0) { add("\\|"); gen(depth + 1); }
			add(rnd(20) ? "\\)" : "");
			if (rnd(3) == 0) add(rnd(5) ? (const char *[]){ "\\@=", "\\@!", "\\@<=", "\\@<!", "\\@>" }[rnd(5)] : PICK(MULTI));
		} else if (k < 17) add(rnd(3) ? "\\|" : "\\&");
		if (rnd(3) == 0) add(PICK(MULTI));
	}
}

static char dir[] = "/tmp/test_keys_regexXXXXXX";

/* the oracle with 'regexpengine' engine on the cases in file in after the
   first skip, added to file out; 5 s at most, as an engine can loop
   forever (the backtracking one on \%d97* and .*x.BaBB with 'ignorecase') */
static int oracle(const char *in, const char *out, int engine, int skip)
{
	char cmd[512];
	snprintf(cmd, sizeof cmd, "cd %s && timeout 5 nvim --headless --clean -l %s/src/keys/regex_oracle.lua %s %s %d %d 2>/dev/null", dir, getcwd((char[256]){ 0 }, 256), in, out, engine, skip);
	return system(cmd);
}

static int count(const char *path)
{
	FILE *f = fopen(path, "r");
	int n = 0;
	for (int c; f && (c = getc(f)) != EOF;) n += c == '\n';
	if (f) fclose(f);
	return n;
}

int main(void)
{
	char text[64], cases[64], out[64], one[64], oneout[64];
	CHECK(mkdtemp(dir));
	if (getenv("SEED")) rng += strtoull(getenv("SEED"), NULL, 10);
	snprintf(text, sizeof text, "%s/text", dir);
	snprintf(cases, sizeof cases, "%s/cases", dir);
	snprintf(out, sizeof out, "%s/out", dir);
	snprintf(one, sizeof one, "%s/one", dir);
	snprintf(oneout, sizeof oneout, "%s/oneout", dir);
	static char lines[N][64], pats[N][256];
	static int ics[N];
	FILE *t = fopen(text, "w"), *f = fopen(cases, "w");
	CHECK(t && f);
	for (int i = 0; i < N; i++) {
		for (unsigned n = rnd(10); n--;) strcat(lines[i], PICK(TEXT));
		pat[0] = 0;
		gen(0);
		if (i < NF) strcpy(pat, FIXED[i][0]), strcpy(lines[i], FIXED[i][1]);
		strcpy(pats[i], pat);
		ics[i] = rnd(3) == 0 && i >= NF;
		fprintf(t, "%s\n", lines[i]);
		fprintf(f, "%d\n%s\n%s\n", ics[i], pat, lines[i]);
	}
	CHECK(!fclose(t) && !fclose(f));
	/* a case Neovim loops on gets T, as Neovim gives no answer */
	for (int hung = 0; oracle(cases, out, 0, count(out)); hung++) {
		FILE *a = fopen(out, "a");
		CHECK(hung < 10 && a && fputs("T\n", a) >= 0 && !fclose(a));
	}
	static Editor e;
	CHECK(!core_open(&e, text));
	E = &e;
	FILE *o = fopen(out, "r");
	CHECK(o);
	int bad = 0;
	for (int i = 0; i < N; i++) {
		char want[4096], bt[4096] = "", got[4096] = "", *g = got;
		CHECK(fgets(want, sizeof want, o));
		want[strcspn(want, "\n")] = 0;
		const char *msg;
		if (re_compile(pats[i], ics[i], &msg)) strcpy(got, "E");
		else {
			/* do_string_sub(): a match from where the last ended; an
			   empty one where the last empty one was skips a character;
			   the end of the line ends it */
			const char *x = lines[i];
			long len = (long)strlen(x), tail = 0, zero = -1;
			Pos s, z;
			for (int r; (r = re_exec(i, tail, &s, &z, NULL, NULL)) != 0;) {
				if (r < 0) { strcpy(got, "T"); break; }
				if (s.col == z.col) {
					if (zero == s.col) {
						long n = (long)uni_glen((const unsigned char *)x, (size_t)len, (size_t)tail);
						g += sprintf(g, "%.*s", (int)n, x + tail);
						tail += n;
						if (tail > len) break;
						continue;
					}
					zero = s.col;
				}
				g += sprintf(g, "%.*s[%.*s]", (int)(s.col - tail), x + tail, (int)(z.col - s.col), x + s.col);
				tail = z.col;
				if (tail >= len) break;
			}
			if (strcmp(got, "T")) sprintf(g, "%s", x + (tail < len ? tail : len));
		}
		/* the NFA engine has bugs of its own (\(e\|\)\. does not match e +
		   U+0301), so a difference stands only if the backtracking one,
		   which regex.c copies, gives it too */
		if (strcmp(got, want) && i >= NF) {
			FILE *b = fopen(one, "w");
			CHECK(b && fprintf(b, "%d\n%s\n%s\n", ics[i], pats[i], lines[i]) > 0 && !fclose(b));
			unlink(oneout);
			strcpy(bt, "T");
			if (!oracle(one, oneout, 1, 0) && (b = fopen(oneout, "r"))) {
				CHECK(fgets(bt, sizeof bt, b));
				bt[strcspn(bt, "\n")] = 0;
				fclose(b);
			}
		}
		int known = 0;
		for (size_t k = 0; k < sizeof KNOWN / sizeof *KNOWN; k++) known |= !strcmp(pats[i], KNOWN[k][0]) && !strcmp(lines[i], KNOWN[k][1]);
		/* T: Neovim gave up (E363, or looped), so it has no answer to copy */
		if (strcmp(got, want) && strcmp(got, bt) && (strcmp(want, "T") || strcmp(bt, "T")) && !known && bad++ < 400)
			fprintf(stderr, "%d: ic %d /%s/ on [%s]: ours %s, Neovim %s (backtracking %s)%s%s\n", i, ics[i], pats[i], lines[i], got, want, bt, msg && got[0] == 'E' ? " -- " : "", msg && got[0] == 'E' ? msg : "");
	}
	fclose(o);
	core_free(&e);
	if (bad) fprintf(stderr, "%d of %d differ; cases in %s\n", bad, N, dir);
	else unlink(text), unlink(cases), unlink(out), unlink(one), unlink(oneout), rmdir(dir);
	CHECK(!bad);
	printf("test_keys_regex: %d random patterns, each match as Neovim's\n", N);
	return 0;
}
