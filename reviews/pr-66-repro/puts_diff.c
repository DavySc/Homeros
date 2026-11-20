/* Review of PR 66: the old writers (copied from main at 4575efd) against the
   new grid_puts on random strings, cell by cell. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "grid.h"
#include "uni.h"

/* main's src/tools/pick.c text() */
static int old_text(Grid *g, int r, int x, int end, const char *s, size_t n, int attr)
{
	const unsigned char *u = (const unsigned char *)s;
	for (size_t i = 0; i < n && x < end;) {
		size_t k = uni_glen(u, n, i);
		int w = u[i] < 0x20 || u[i] == 0x7f ? 0 : uni_width(u, n, i);
		if (w > 2) w = 0;
		if (x + (w ? w : 1) > end) break;
		if (!w) grid_put(g, r, x, (const unsigned char *)" ", 1, 1, attr);
		else grid_put(g, r, x, u + i, k, w, attr);
		x += w ? w : 1; i += k;
	}
	return x;
}
/* main's grid_puts */
static int old_puts(Grid *g, int r, int c, const char *s, int attr)
{
	for (; *s; s++) c += grid_put(g, r, c, (const unsigned char *)s, 1, 1, attr);
	return c;
}
/* main's command line in core_draw; returns the cursor column */
static int old_cmd(Grid *g, int r, const char *cmd, size_t cmdlen, size_t cmdpos)
{
	char t[2] = { ':', 0 };
	int c = old_puts(g, r, 0, t, ATTR_NONE), cc = c;
	const unsigned char *s = (const unsigned char *)cmd;
	for (size_t i = 0; i < cmdlen; i += uni_glen(s, cmdlen, i)) {
		if (i == cmdpos) cc = c;
		c += grid_put(g, r, c, s + i, uni_glen(s, cmdlen, i), uni_width(s, cmdlen, i), ATTR_NONE);
	}
	if (cmdpos >= cmdlen) cc = c;
	return cc;
}
static int new_cmd(Grid *g, int r, const char *cmd, size_t cmdlen, size_t cmdpos)
{
	char t[2] = { ':', 0 };
	int cc = grid_puts(g, r, 1, g->cols, cmd, cmdpos, ATTR_NONE, FACE_NONE);
	grid_puts(g, r, 0, 1, t, 1, ATTR_NONE, FACE_NONE);
	grid_puts(g, r, cc, g->cols, cmd + cmdpos, cmdlen - cmdpos, ATTR_NONE, FACE_NONE);
	return cc;
}

static unsigned long long rs = 88172645463325252ull;
static unsigned rnd(unsigned n) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (unsigned)(rs % n); }

static const char *pool[5][12] = {
	/* 0: printable ASCII */ { "a", "Z", " ", "~", "0", "/", ".", "_", "-", "x", "(", ":" },
	/* 1: + narrow non-ASCII */ { "a", "é", "ß", "λ", "→", "│", "x", " ", "ö", "Ж", "n", "." },
	/* 2: + wide, emoji, combining after a base */ { "a", "全", "角", "e\xcc\x81", "😀", "❤\xef\xb8\x8f", "x", " ", "한", "é", "👍🏽", "." },
	/* 3: + control characters */ { "a", "\t", "\x01", "\x1b", "\x7f", "全", "x", " ", "é", "\r", "b", "." },
	/* 4: + invalid bytes, C1, a lone combining mark, unprintable */ { "a", "\x80", "\xff", "\xc3", "\xcc\x81", "\xc2\x85", "x", "全", "\xe2\x80\xa8", "\xef\xbf\xbe", "b", "\xed\xa0\x80" },
};

static int same(const Grid *a, const Grid *b)
{
	for (int i = 0; i < a->rows * a->cols; i++) if (!grid_cell_eq(&a->c[i], &b->c[i])) return 0;
	return 1;
}

int main(int argc, char **argv)
{
	int N = argc > 1 ? atoi(argv[1]) : 100000, shown = 0;
	Grid a = { 0 }, b = { 0 };
	grid_resize(&a, 1, 40); grid_resize(&b, 1, 40);
	static const char *what[3] = { "pick text()", "command line", "ASCII grid_puts" };
	for (int cls = 0; cls < 5; cls++) {
		int diff[3] = { 0 }, n3 = 0;
		for (int it = 0; it < N; it++) {
			char s[256]; size_t n = 0, cut[32]; int k = (int)rnd(24), nc = 0;
			for (int j = 0; j < k; j++) { const char *p = pool[cls][rnd(12)]; cut[nc++] = n; memcpy(s + n, p, strlen(p)); n += strlen(p); }
			cut[nc++] = n; s[n] = 0;
			int x0 = (int)rnd(6), end = 40 - (int)rnd(30);
			/* pick's text against grid_puts */
			grid_clear(&a); grid_clear(&b);
			int ra = old_text(&a, 0, x0, end, s, n, ATTR_REV), rb = grid_puts(&b, 0, x0, end, s, n, ATTR_REV, FACE_NONE);
			if (ra != rb || !same(&a, &b)) { if (!diff[0]++ && shown < 12) { shown++; printf("  class %d %s: old ->%d new ->%d for", cls, what[0], ra, rb); for (size_t i = 0; i < n; i++) printf(" %02x", (unsigned char)s[i]); puts(""); } }
			/* the command line, the cursor at a piece boundary */
			size_t pos = cut[rnd((unsigned)nc)];
			grid_clear(&a); grid_clear(&b);
			ra = old_cmd(&a, 0, s, n, pos); rb = new_cmd(&b, 0, s, n, pos);
			if (ra != rb || !same(&a, &b)) { if (!diff[1]++ && shown < 12) { shown++; printf("  class %d %s: cursor old %d new %d (pos %zu) for", cls, what[1], ra, rb, pos); for (size_t i = 0; i < n; i++) printf(" %02x", (unsigned char)s[i]); puts(""); } }
			/* the old ASCII grid_puts (messages, titles); only strings it took: no NUL inside */
			if (cls == 0) {
				n3++;
				grid_clear(&a); grid_clear(&b);
				ra = old_puts(&a, 0, x0, s, ATTR_DIM); rb = grid_puts(&b, 0, x0, a.cols, s, n, ATTR_DIM, FACE_NONE);
				if (ra != rb || !same(&a, &b)) diff[2]++;
			}
		}
		printf("class %d: %d strings; differ: %s %d, %s %d", cls, N, what[0], diff[0], what[1], diff[1]);
		if (n3) printf(", %s %d", what[2], diff[2]);
		puts("");
	}
	grid_free(&a); grid_free(&b); return 0;
}
