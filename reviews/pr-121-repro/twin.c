/* PR 121 review: random edits on twins.  A parses whole at each parse
   point (the flow test_syntax checks against Neovim), B's parses are
   stopped at random points and may be unfinished when the next edit comes,
   F is opened fresh on the final text.  Counts the cases whose fold
   answers differ.  argv[1]: cases per language (default 2000). */
#include <string.h>
#include "check.h"
#include "syntax.h"
#include "text.h"
static size_t r1, r2;
static const char *const tok[7][16] = {
	{ "\n", "/*", "*/", "\"", "'", "{", "}", "(", ")", "x", " ", "\xc3\xa9", "#define A(x) x\n", "int", "FOO", ";" },
	{ "\n", "--", "[[", "]]", "\"", "'", "(", ")", "x", " ", "\xc3", "local", "function", "end", "--- @param", "vim.cmd" },
	{ "\n", "#", "```", "```c\n", "```lua\n", "*", "1. ", "`", "[", "](", ")", "- ", "| a |", "\n\n", "\xa9", "> " },
	{ "\n", "r#\"", "\"#", "\"", "/*", "*/", "//", "{", "}", "(", ")", " ", "x", "println!(", "fn f<'a>(", "'a" },
	{ "\n", "#", "/*", "*/", "''", "\"", "{", "}", "${", ";", "x", "\\", "/* lua */ ''", "# rust\n", "pkgs.writeRust \"a\" {} ''", "''$" },
	{ "\n", "#", "#[[", "]]", "[==[", "]==]", "\"", "${", "}", "(", ")", " ", "x", "\\", "set(", "endif()" },
	{ "\n", "//", "/*", "*/", "\"", "(", ")", ";", "x", " ", "`define A", "`A", "module m;", "endmodule", "begin", "end" },
};
static const char *const path[7] = { "/tmp/f.c", "/tmp/f.lua", "/tmp/f.md", "/tmp/f.rs", "/tmp/f.nix", "/tmp/f.cmake", "/tmp/f.sv" };
static int left;
static int stop(void) { return left-- <= 0; }
static void folds(Syntax *s, const Text *t, char *o, size_t cap)
{
	o[0] = 0;
	for (size_t l = 0, k = 0; l < text_lines(t) && k + 8 < cap; l++) {
		int st, v = syntax_foldexpr(s, l, &st);
		k += (size_t)snprintf(o + k, cap - k, " %s%d", st ? ">" : "", v);
	}
}
int main(int argc, char **argv)
{
	int N = argc > 1 ? atoi(argv[1]) : 2000, show = 3;
	static unsigned char f[1 << 14];
	static char fa[1 << 14], fb[1 << 14], ff[1 << 14];
	for (int lang = 0; lang < 7; lang++) {
		int af = 0, bf = 0, ba = 0, unf = 0;
		for (int c = 0; c < N; c++) {
			if (getenv("TRACE") && atoi(getenv("TRACE")) == lang * 100000 + c) setenv("DBG", "1", 1); else unsetenv("DBG");
			char b[4096];
			size_t bn = 0;
			int tr0 = getenv("TRACE") && atoi(getenv("TRACE")) == lang * 100000 + c;
			for (unsigned k = 4 + rnd(40); k--; ) {
				const char *s = tok[lang][rnd(4) ? rnd(16) : 0];
				memcpy(b + bn, s, strlen(s)), bn += strlen(s);
			}
			b[bn++] = '\n';
			Text *t = text_new(b, bn);
			if (tr0) printf("text: [%.*s]\n", (int)bn, b);
			Syntax *A = syntax_open(path[lang], t), *B = syntax_open(path[lang], t);
			CHECK(A && B);
			syntax_faces(A, 0, text_len(t), f), syntax_faces(B, 0, text_len(t), f);
			while (syntax_update(A, t, NULL, &r1, &r2) == 2) ;
			while (syntax_update(B, t, NULL, &r1, &r2) == 2) ;
			int open = 0;
			for (int e = 1 + (int)rnd(12); e--; ) {
				size_t len = text_len(t), pos = rnd((unsigned)len), del = rnd(8);
				if (del > len - 1 - pos) del = len - 1 - pos;
				const char *ins = tok[lang][rnd(3) ? rnd(16) : 0];
				if (len + strlen(ins) > 8000) break;
				syntax_edit(A, t, pos, del, ins, strlen(ins));
				syntax_edit(B, t, pos, del, ins, strlen(ins));
				CHECK(!text_delete(t, pos, del) && !text_insert(t, pos, ins, strlen(ins)));
				int tr = getenv("TRACE") && atoi(getenv("TRACE")) == lang * 100000 + c;
				if (tr) { printf("edit pos %zu del %zu ins [", pos, del); for (const char *i = ins; *i; i++) printf(*i == '\n' ? "\\n" : "%c", *i); printf("]\n"); }
				if (rnd(2)) {
					syntax_faces(A, 0, text_len(t), f), syntax_faces(B, 0, text_len(t), f);
					while (syntax_update(A, t, NULL, &r1, &r2) == 2) ;
					left = (int)rnd(6);
					int q = syntax_update(B, t, stop, &r1, &r2);
					open |= q == 2;
					if (tr) folds(A, t, fa, sizeof fa), folds(B, t, fb, sizeof fb), printf(" parse: B returned %d\n  A:%s\n  B:%s\n", q, fa, fb);
				}
			}
			syntax_faces(A, 0, text_len(t), f), syntax_faces(B, 0, text_len(t), f);
			while (syntax_update(A, t, NULL, &r1, &r2) == 2) ;
			while (syntax_update(B, t, NULL, &r1, &r2) == 2) ;
			Syntax *F = syntax_open(path[lang], t);
			syntax_faces(F, 0, text_len(t), f);
			while (syntax_update(F, t, NULL, &r1, &r2) == 2) ;
			folds(A, t, fa, sizeof fa), folds(B, t, fb, sizeof fb), folds(F, t, ff, sizeof ff);
			af += strcmp(fa, ff) != 0, bf += strcmp(fb, ff) != 0, unf += open;
			if (strcmp(fa, fb)) {
				ba++;
				if (show-- > 0) printf("%s case %d\n  whole:  %s\n  stopped:%s\n  fresh:  %s\n", path[lang], c, fa, fb, ff);
			}
			syntax_free(A), syntax_free(B), syntax_free(F), text_free(t);
		}
		printf("%-12s %d cases (%d with a parse left unfinished before an edit): whole != fresh %d, stopped != fresh %d, stopped != whole %d\n",
		       path[lang], N, unf, af, bf, ba);
	}
	return 0;
}
