/* Review of PR 42: one file, a list of edits, each followed by the frame a
   key gets (no parse in between unless "u" is given): prints every byte
   whose face differs from a full recompute over the same trees, and from
   the frame after the parse.
   usage: directed FILE { [n] POS DEL INS | u }...   (n: the next edit gets no frame)   (TOP=line ROWS=n: the window) */
#include "../../src/syntax/syntax.c"
#include <stdio.h>

static Text *t;
static Syntax *s;
static unsigned char F[1 << 16], G[1 << 16], H[1 << 16];

static void frame(unsigned char *out)
{
	size_t nl = text_lines(t), top = getenv("TOP") ? (size_t)atol(getenv("TOP")) : 0;
	if (getenv("ROWS") && top + (size_t)atol(getenv("ROWS")) < nl) nl = top + (size_t)atol(getenv("ROWS"));
	memset(out, 0, text_len(t));
	for (size_t l = top; l < nl; l++) {
		size_t a = text_line_start(t, l), z = text_line_start(t, l + 1);
		syntax_faces(s, a, z, out + a);
	}
}

static void show(const char *what, const unsigned char *x, const unsigned char *y)
{
	size_t n = text_len(t), k = 0;
	for (size_t i = 0; i < n; i++)
		if (x[i] != y[i]) {
			char c;
			text_read(t, i, &c, 1);
			if (k++ < 12) printf("   byte %zu '%c': shown face %d, %s %d\n", i, c == '\n' ? '$' : c, x[i], what, y[i]);
		}
	if (k) printf("   %zu bytes differ from %s\n", k, what);
}

int main(int argc, char **argv)
{
	static char buf[1 << 16];
	FILE *f = fopen(argv[1], "rb");
	if (!f) return 2;
	size_t n = fread(buf, 1, sizeof buf, f);
	fclose(f);
	t = text_new(buf, n);
	s = syntax_open(argv[1], t);
	if (!t || !s) return 2;
	frame(F);
	while (syntax_update(s, t, NULL) == 2) ;
	frame(F);
	int bad = 0;
	for (int i = 2; i < argc; i++) {
		if (argv[i][0] == 'u') {
			while (syntax_update(s, t, NULL) == 2) ;
			frame(F);
			continue;
		}
		int quiet = argv[i][0] == 'n';        /* no frame after this edit */
		i += quiet;
		size_t pos = (size_t)atol(argv[i]), del = (size_t)atol(argv[i + 1]);
		const char *ins = argv[i + 2];
		i += 2;
		syntax_edit(s, t, pos, del, ins, strlen(ins));
		if (text_delete(t, pos, del) || text_insert(t, pos, ins, strlen(ins))) return 2;
		if (quiet) continue;
		frame(F);
		s->cz = 0;
		frame(G);
		while (syntax_update(s, t, NULL) == 2) ;
		frame(H);
		printf("edit %zu -%zu +\"%s\":\n", pos, del, ins);
		show("a full recompute", F, G);
		show("the frame after the parse", F, H);
		bad |= memcmp(F, G, text_len(t)) != 0;
	}
	return bad;
}
