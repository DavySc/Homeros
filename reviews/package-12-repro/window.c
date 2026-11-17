/* Review of package 12: the editor asks for a window, not the whole text.
   test_syntax.c always shows the whole text before a parse, so the code
   that limits injections and parses to the window (ra/rz, pa/pz, the skip of
   regions outside) has no oracle there.  Here one Syntax (W) is driven as
   src/main.c drives it: faces for a 40-line window line by line, update,
   faces again, with random scrolls and edits; the other (F) is shown the
   whole text before each parse.  After each parse, W's window must equal
   F's colours of the same lines asked the same way (line by line from the
   window's first line).  Counts windows that differ and prints the first.
   usage: window FILE ROUNDS SEED */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "syntax.h"
#include "text.h"

static unsigned long long rng;
static unsigned rnd(unsigned n) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return (unsigned)(rng % n); }
static unsigned char fw[1 << 22], ff[1 << 22], all[1 << 22];

static void ask(Syntax *s, const Text *t, size_t l0, size_t nl, unsigned char *f)
{
	for (size_t l = l0; l < l0 + nl; l++) {
		size_t a = text_line_start(t, l), z = text_line_start(t, l + 1);
		syntax_faces(s, a, z, f + a);
	}
}

int main(int argc, char **argv)
{
	if (argc != 4) return 2;
	FILE *f = fopen(argv[1], "rb");
	if (!f) return 1;
	static char b[1 << 21];
	size_t n = fread(b, 1, sizeof b, f);
	fclose(f);
	int rounds = atoi(argv[2]), bad = 0, wins = 0;
	rng = 2 * strtoull(argv[3], NULL, 10) + 1;
	Text *t = text_new(b, n);
	Syntax *W = syntax_open(argv[1], t), *F = syntax_open(argv[1], t);
	if (!W || !F) return 3;
	static const char *const ins[] = { "x", "\n", "/*", "*/", "\"", "{", "}", "`", "*", "[", "](", "--", "[[", "]]", "\n\n", "```c\n" };
	size_t l0 = 0;
	/* F's schedule in tests/test_syntax.lua's format, to replay in Neovim */
	char on[80];
	mkdir("build/replay", 0777);
	snprintf(on, sizeof on, "build/replay/0%s", strrchr(argv[1], '.'));
	FILE *o0 = fopen(on, "wb");
	fwrite(b, 1, n, o0);
	fclose(o0);
	strcat(on, ".ops");
	FILE *ops = fopen(on, "w");
	for (int r = 0; r < rounds; r++) {
		size_t lines = text_lines(t), nl = 40;
		unsigned op = rnd(4);
		if (op == 0) l0 = rnd((unsigned)lines);                         /* jump */
		else if (op == 1) l0 = l0 + 3 < lines ? l0 + 3 : l0;            /* scroll */
		else {                                                          /* type in the window */
			size_t a = text_line_start(t, l0), z = text_line_start(t, l0 + nl < lines ? l0 + nl : lines);
			size_t pos = a + (z > a ? rnd((unsigned)(z - a)) : 0), del = rnd(3), len = text_len(t);
			if (pos + del >= len) del = 0;
			if (pos >= len) pos = len ? len - 1 : 0;
			const char *s = ins[rnd(16)];
			if (len + strlen(s) >= sizeof fw) continue;
			syntax_edit(W, t, pos, del, s, strlen(s));
			syntax_edit(F, t, pos, del, s, strlen(s));
			if (text_delete(t, pos, del) || text_insert(t, pos, s, strlen(s))) return 4;
			lines = text_lines(t);
			fprintf(ops, "e %zu %zu ", pos, del);
			for (const char *c = s; *c; c++) fprintf(ops, "%02x", (unsigned char)*c);
			fprintf(ops, "\np\n");
		}
		if (l0 >= lines) l0 = lines - 1;
		if (l0 + nl > lines) nl = lines - l0;
		ask(W, t, l0, nl, fw);                         /* the frame */
		while (syntax_update(W, t, NULL)) ;            /* the parse, and its redraws */
		ask(W, t, l0, nl, fw);
		syntax_faces(F, 0, text_len(t), all);
		while (syntax_update(F, t, NULL)) ;
		ask(F, t, l0, nl, ff);
		size_t a = text_line_start(t, l0), z = text_line_start(t, l0 + nl);
		wins++;
		if (memcmp(fw + a, ff + a, z - a)) {
			if (!bad++) {
				size_t i = a;
				while (fw[i] == ff[i]) i++;
				fprintf(stderr, "round %d: window from line %zu: byte %zu (line %zu) face %d in the window, %d with the whole text shown\n",
				        r, l0, i, text_line_of(t, i), fw[i], ff[i]);
				/* the text at this moment, to open in Neovim (argv[1]'s ending kept) */
				char out[64];
				snprintf(out, sizeof out, "build/first-diff%s", strrchr(argv[1], '.'));
				FILE *o = fopen(out, "wb");
				static char tb[1 << 22];
				size_t tn = text_read(t, 0, tb, sizeof tb);
				if (o) { fwrite(tb, 1, tn, o); fclose(o); }
				fprintf(stderr, "text written to %s; W's and F's faces of line %zu:\n", out, text_line_of(t, i));
				size_t la = text_line_start(t, text_line_of(t, i)), lz = text_line_start(t, text_line_of(t, i) + 1);
				for (size_t k = la; k + 1 < lz; k++) fputc('0' + fw[k], stderr);
				fputc('\n', stderr);
				for (size_t k = la; k + 1 < lz; k++) fputc('0' + ff[k], stderr);
				fputc('\n', stderr);
				/* F's faces of the whole text, line by line from the first */
				fclose(ops);
				ask(F, t, 0, text_lines(t), all);
				FILE *fo = fopen("build/replay/F.faces", "wb");
				for (size_t k = 0; k < tn; k++) fputc(tb[k] == '\n' ? '\n' : '0' + all[k], fo);
				fclose(fo);
				syntax_free(W); syntax_free(F); text_free(t);
				return 1;
			}
		}
	}
	printf("%s: %d of %d windows differ from the whole-text colours\n", argv[1], bad, wins);
	syntax_free(W);
	syntax_free(F);
	text_free(t);
	return bad != 0;
}
