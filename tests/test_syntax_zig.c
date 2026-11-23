/* Zig's colours, folds and zig fmt from Zig's own parser (zig.zig through
   syntax.c), on the std of the Zig release nixpkgs builds
   (build/syntax/src/corpus/zig: its zig/ and json/, 2.2 MB):
   - colours and folds against Neovim 0.12 with nvim-treesitter's Zig
     grammar and queries (tests/test_syntax.lua), on the files tree-sitter
     parses without errors: every line's fold the same, every non-blank
     byte's face the same except the runs tests/syntax-zig-known.txt lists, where
     tree-sitter-zig misreads Zig or colours a field named null as the
     keyword (each judged in research/zig.md, prototype C; the list only
     shrinks);
   - after random edits, the colours and folds a fresh open gives the text,
     and no crash on the broken code the edits make;
   - zig fmt: a file with every line's indent taken away comes back as it
     was (std is zig fmt's output; but where "zig fmt: off" keeps it), for the release on PATH (nixpkgs'
     zig, the corpus's release); each other release's zig fmt gives its own
     output back unchanged. */
#include <dirent.h>
#include <stdint.h>
#include <string.h>
#include "check.h"
#include "grid.h"
#include "syntax.h"
#include "text.h"

typedef struct ZigH ZigH;
#define REL(v) \
	ZigH *zig##v##_parse(const char *src, size_t n, unsigned char *face); \
	void zig##v##_free(ZigH *h); \
	const char *zig##v##_fmt(ZigH *h, size_t *n);
REL(15) REL(16) REL(17)
static ZigH *(*const rparse[])(const char *, size_t, unsigned char *) = { zig15_parse, zig16_parse, zig17_parse };
static void (*const rfree[])(ZigH *) = { zig15_free, zig16_free, zig17_free };
static const char *(*const rfmt[])(ZigH *, size_t *) = { zig15_fmt, zig16_fmt, zig17_fmt };

enum { MAXF = 64, EDITS = 40 };
static char *src[MAXF], name[MAXF][320];
static size_t len[MAXF];
static int nf;
static size_t r1, r2;
static char dir[] = "/tmp/test_syntax_zig_XXXXXX";

static char *slurp(const char *path, size_t *n)
{
	FILE *f = fopen(path, "rb");
	CHECK(f);
	CHECK(!fseek(f, 0, SEEK_END));
	long l = ftell(f);
	CHECK(l >= 0 && !fseek(f, 0, SEEK_SET));
	char *r = malloc((size_t)l + 1);
	CHECK(r && fread(r, 1, (size_t)l, f) == (size_t)l);
	fclose(f);
	r[l] = 0;
	*n = (size_t)l;
	return r;
}

static void spit(const char *path, const char *s, size_t n)
{
	FILE *f = fopen(path, "wb");
	CHECK(f && fwrite(s, 1, n, f) == n);
	fclose(f);
}

/* in name order, as test_syntax.c's corpus */
static void add_dir(const char *sub)
{
	char d[64];
	snprintf(d, sizeof d, "build/syntax/src/corpus/zig/%s", sub);
	struct dirent **de;
	int nd = scandir(d, &de, NULL, alphasort);
	CHECK(nd > 0);
	for (int i = 0; i < nd; i++) {
		size_t l = strlen(de[i]->d_name);
		if (nf < MAXF && l > 4 && !strcmp(de[i]->d_name + l - 4, ".zig")) {
			char p[400];
			snprintf(p, sizeof p, "%s/%s", d, de[i]->d_name);
			snprintf(name[nf], sizeof name[nf], "%s/%s", sub, de[i]->d_name);
			src[nf] = slurp(p, &len[nf]);
			nf++;
		}
		free(de[i]);
	}
	free(de);
}

/* the faces as core_draw asks for them, line by line, then as
   test_syntax.lua writes them: one char per byte, '\n' kept */
static void draw(Syntax *s, const Text *t, char *out)
{
	static unsigned char f[1 << 16];
	for (size_t l = 0, n = text_lines(t); l <= n; l++) {
		size_t a = text_line_start(t, l), z = text_line_start(t, l + 1), c;
		for (; a < z; a += c) {
			c = z - a < sizeof f ? z - a : sizeof f;
			syntax_faces(s, a, a + c, f);
			char b[sizeof f];
			text_read(t, a, b, c);
			for (size_t i = 0; i < c; i++) {
				CHECK(f[i] < FACE_COUNT);
				out[a + i] = b[i] == '\n' ? '\n' : (char)('0' + f[i]);
			}
		}
	}
	out[text_len(t)] = 0;
}

/* each line's foldexpr, as test_syntax.lua writes vim.treesitter.foldexpr() */
static void folds(Syntax *s, const Text *t, char *out)
{
	size_t n = 0;
	for (size_t l = 0, nl = text_lines(t); l < nl; l++) {
		int st, v = syntax_foldexpr(s, l, &st);
		n += (size_t)sprintf(out + n, "%s%s%d", l ? " " : "", st ? ">" : "", v);
	}
	strcpy(out + n, "\n");
}

static Syntax *open(const Text *t)
{
	Syntax *s = syntax_open("x.zig", t);
	CHECK(s);
	CHECK(syntax_update(s, t, NULL, &r1, &r2) == 1);
	return s;
}

int main(void)
{
	add_dir("zig");
	add_dir("json");
	CHECK(mkdtemp(dir));
	char cmd[1 << 14], path[512];
	int n = snprintf(cmd, sizeof cmd, "nvim --headless --clean -l tests/test_syntax.lua %s/out %s", dir, dir);
	for (int i = 0; i < nf; i++) {
		snprintf(path, sizeof path, "%s/%d.zig", dir, i);
		spit(path, src[i], len[i]);
		strcat(path, ".ops");
		spit(path, "", 0);
		n += snprintf(cmd + n, sizeof cmd - (size_t)n, " %d.zig", i);
	}
	CHECK(!system(cmd));
	size_t on, en, fn, kn;
	snprintf(path, sizeof path, "%s/out", dir);
	char *want = slurp(path, &on), *wp = want;
	strcat(path, ".err");
	char *err = slurp(path, &en);
	snprintf(path, sizeof path, "%s/out.fold", dir);
	char *wfold = slurp(path, &fn), *fp = wfold;
	char *known = slurp("tests/syntax-zig-known.txt", &kn);
	CHECK(en == (size_t)nf);
	int compared = 0, unknown = 0;
	long diff = 0;
	static char got[1 << 22], gfold[1 << 20];
	for (int i = 0; i < nf; i++) {
		Text *t = text_new(src[i], len[i]);
		CHECK(t);
		Syntax *s = open(t);
		draw(s, t, got);
		folds(s, t, gfold);
		char *fe = strchr(fp, '\n');
		CHECK(fe);
		if (err[i] == '0') {
			compared++;
			for (size_t k = 0; k < len[i]; k++) CHECK((wp[k] == '\n') == (got[k] == '\n'));   /* the same text */
			/* blanks show no face (no Zig capture has a background) */
			for (size_t k = 0; k < len[i]; k++)
				if (src[i][k] == ' ') got[k] = wp[k];
			for (size_t a = 0, z; a < len[i]; a = z) {
				for (z = a; z < len[i] && wp[z] != got[z]; z++) ;
				if (z == a) { z++; continue; }
				char line[128];
				snprintf(line, sizeof line, "%s %zu %zu %d %d\n", name[i], a, z - a, wp[a] - '0', got[a] - '0');
				diff += (long)(z - a);
				if (!strstr(known, line)) unknown++, fputs(line, stderr);
			}
			if ((size_t)(fe + 1 - fp) != strlen(gfold) || memcmp(fp, gfold, strlen(gfold))) {
				fprintf(stderr, "%s: folds differ from Neovim's\n", name[i]);
				CHECK(0);
			}
		}
		wp += len[i], fp = fe + 1;
		/* random edits, each followed by a parse, against a fresh open */
		for (int e = 0; e < EDITS; e++) {
			static const char *const snip[] = { "\n", "//", "{", "}", "(", ")", ";", ",", "|x|", "\"", "fn f() void {", "const x = ", "if (a) ", "else ", ".{", "\\\\" };
			size_t at = rnd((unsigned)text_len(t) + 1), del = rnd(8);
			if (at + del > text_len(t)) del = text_len(t) - at;
			const char *ins = rnd(2) ? snip[rnd(sizeof snip / sizeof *snip)] : "";
			size_t ni = strlen(ins), was = text_len(t);
			static char g2[1 << 22], f2[1 << 20];
			draw(s, t, got);
			syntax_edit(s, t, at, del, ins, ni);
			CHECK(!text_delete(t, at, del) && !text_insert(t, at, ins, ni));
			/* until the parse, the faces move with the text */
			draw(s, t, g2);
			CHECK(!memcmp(g2, got, at) && !memcmp(g2 + at + ni, got + at + del, was - at - del));
			if (rnd(3)) continue;   /* edits pile up between parses, as between frames */
			CHECK(syntax_update(s, t, NULL, &r1, &r2) == 1);
			Syntax *f = open(t);
			draw(s, t, got), draw(f, t, g2);
			folds(s, t, gfold), folds(f, t, f2);
			CHECK(!strcmp(got, g2) && !strcmp(gfold, f2));
			syntax_free(f);
		}
		syntax_free(s);
		text_free(t);
	}
	CHECK(compared >= nf / 2);
	/* a top-level member the parse found an error in gets token colours
	   only (zig.zig's broken: std's lastToken can unwrap a null in it), the
	   others all theirs */
	static const char B[] = "fn f() void {\n    g(1);\n    x.fn y();\n}\nfn h() void {\n    g(1);\n}\n";
	Text *bt = text_new(B, sizeof B - 1);
	CHECK(bt);
	Syntax *bs = open(bt);
	unsigned char bf[sizeof B];
	syntax_faces(bs, 0, sizeof B - 1, bf);
	CHECK(bf[18] == FACE_VARIABLE && bf[strstr(B, "h()") - B + 15] == FACE_FUNCTION_CALL);
	CHECK(B[18] == 'g' && B[strstr(B, "h()") - B + 15] == 'g');
	syntax_free(bs);
	text_free(bt);
	printf("syntax_zig: %d of %d files compared with Neovim, %ld bytes in the known differences\n", compared, nf, diff);
	if (unknown) {
		fprintf(stderr, "%d differences from Neovim not in tests/syntax-zig-known.txt (above)\n", unknown);
		CHECK(0);
	}

	/* zig fmt */
	int same = 0;
	for (int i = 0; i < nf; i++) {
		char *b = malloc(len[i] + 1);
		CHECK(b);
		size_t m = 0;
		for (size_t k = 0; k < len[i]; k++)
			if (src[i][k] != ' ' || (m && b[m - 1] != '\n')) b[m++] = src[i][k];
		Text *t = text_new(b, m);
		CHECK(t);
		Syntax *s = syntax_open("x.zig", t);
		CHECK(s);
		const char *res;
		size_t rn;
		int r = syntax_zig_fmt(s, &res, &rn);
		CHECK(r == 0 || (r == -1 && err[i] == '1'));
		if (!r && !strstr(src[i], "zig fmt: off")) {   /* where zig fmt keeps the indents it is given */
			if (rn != len[i] || memcmp(res, src[i], rn)) {
				fprintf(stderr, "%s: zig fmt of it without indents is not it\n", name[i]);
				CHECK(0);
			}
			same++;
		}
		syntax_free(s);
		text_free(t);
		for (int v = 0; v < 3; v++) {
			ZigH *h = rparse[v](src[i], len[i], NULL), *h2;
			CHECK(h);
			const char *o = rfmt[v](h, &rn), *o2;
			if (o) {
				char *c = malloc(rn + 1);
				CHECK(c);
				memcpy(c, o, rn), c[rn] = 0;
				size_t rn2;
				CHECK((h2 = rparse[v](c, rn, NULL)) && (o2 = rfmt[v](h2, &rn2)) && rn2 == rn && !memcmp(o2, c, rn));
				rfree[v](h2);
				free(c);
			}
			rfree[v](h);
		}
		free(b);
	}
	CHECK(same >= nf / 2);
	/* a text that does not parse is refused, as zig fmt refuses it; a ** 2
	   parses before 0.17 only, so this is also the release on PATH's parser
	   (measured: 0.15 and 0.16 format it) */
	Text *t = text_new("const a = a ** 2;", 17);
	Syntax *s = syntax_open("x.zig", t);
	const char *res;
	size_t rn;
	CHECK(syntax_zig_fmt(s, &res, &rn) == -1);
	syntax_free(s);
	text_free(t);
	printf("syntax_zig: zig fmt gave back %d of %d files\n", same, nf);
	snprintf(cmd, sizeof cmd, "rm -rf %s", dir);
	CHECK(!system(cmd));
	for (int i = 0; i < nf; i++) free(src[i]);
	free(want), free(err), free(wfold), free(known);
	return 0;
}
