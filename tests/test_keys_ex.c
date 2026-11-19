/* Ex ranges and :<line>, against Neovim on random command lines
   (src/keys/ex_oracle.lua): the cursor and the error number after each.
   Marks a-z are not set by the keys yet, so 'a fails in both. */
#include <string.h>
#include <unistd.h>
#include "check.h"
#include "core.h"
#include "keys.h"

enum { N = 3000 };
static const char TEXT[] = "abcdef\n  xy\nhello world\n\tq\nlast line\nz\n";
static const char *const PRE[] = { "", "j", "jj", "G", "lll", "jllll", "jjj", "Vj\x1b", "jvjj\x1bgg", "3j", "Vj" };
static const char *const TOK[] = { "1", "2", "3", "0", "6", "99", ".", "$", "'<", "'>", "'a", "+", "-", "+2",
                                   "-3", "4", " ", ",", ";", "%", ":", "Z" };

int main(void)
{
	char dir[] = "/tmp/test_keys_exXXXXXX", text[64], cases[64], out[64], cmd[256], k[64];
	CHECK(mkdtemp(dir));
	snprintf(text, sizeof text, "%s/text", dir);
	snprintf(cases, sizeof cases, "%s/cases", dir);
	snprintf(out, sizeof out, "%s/out", dir);
	FILE *f = fopen(text, "w");
	CHECK(f && fputs(TEXT, f) >= 0 && !fclose(f));
	CHECK((f = fopen(cases, "w")));
	for (int i = 0; i < N; i++) {
		strcpy(k, PRE[rnd(sizeof PRE / sizeof *PRE)]);
		if (!rnd(4)) strcat(k, rnd(2) ? "2" : "3");
		/* Neovim's command line takes a ":" typed after "'<,'>" (Visual
		   mode's or typed, blanks between) differently from one after
		   "1,2" (measured, 3 runs; not understood yet): left out */
		int vis = k[0] == 'V' && k[2] != 27;
		strcat(k, ":");
		for (unsigned t = rnd(5); t--;) {
			const char *x = TOK[rnd(sizeof TOK / sizeof *TOK)];
			size_t l = strlen(k);
			while (k[l - 1] == ' ') l--;
			if (strcmp(x, ":") || (vis ? k[l - 1] != ':' : l < 5 || strncmp(k + l - 5, "'<,'>", 5))) strcat(k, x);
		}
		fprintf(f, "%s\r\n", k);
	}
	CHECK(!fclose(f));
	snprintf(cmd, sizeof cmd, "nvim --headless --clean -l src/keys/ex_oracle.lua %s %s %s 2>/dev/null", text, cases, out);
	CHECK(!system(cmd));
	FILE *c = fopen(cases, "r"), *o = fopen(out, "r");
	CHECK(c && o);
	int bad = 0;
	for (int i = 0; i < N; i++) {
		long line, col;
		char err[16];
		CHECK(fgets(k, sizeof k, c) && fscanf(o, "%ld %ld %15s", &line, &col, err) == 3);
		static Editor e;
		CHECK(!core_open(&e, text));
		for (char *s = k; *s != '\n'; s++) keys_feed(&e, *s == '\r' ? KEY_ENTER : *s == 27 ? KEY_ESC : (unsigned char)*s, s[1] != '\n');
		const char *m = e.msg[0] == 'E' ? e.msg : "-";
		if ((long)e.win->line + 1 != line || (long)e.win->col != col || strncmp(m, err, strlen(err)) || (m[0] == 'E' && m[strlen(err)] != ':')) {
			k[strlen(k) - 2] = 0;
			if (bad++ < 10) fprintf(stderr, "%d %s: line %zu col %zu [%s], Neovim %ld %ld %s\n", i, k, e.win->line + 1, e.win->col, e.msg, line, col, err);
		}
		core_free(&e);
	}
	fclose(c), fclose(o);
	/* past 2^63 Neovim's line numbers wrap (":9223372036854775806+5" goes
	   to line 3, measured once); ours saturate, without overflow */
	static Editor e;
	CHECK(!core_open(&e, text));
	for (const char *s = ":99999999999999999999+99999999999999999999+9999999999999999999\r"; *s; s++) keys_feed(&e, *s == '\r' ? KEY_ENTER : *s, s[1] != 0);
	CHECK(e.win->line == 5 && !e.msg[0]);
	core_free(&e);
	if (bad) fprintf(stderr, "cases and Neovim's answers in %s\n", dir);
	else unlink(text), unlink(cases), unlink(out), rmdir(dir);
	CHECK(!bad);
	printf("test_keys_ex: %d random ranges, each as Neovim (cursor, error)\n", N);
	return 0;
}
