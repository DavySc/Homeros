/* Review of PR 30: edits, faces and frees while a release is half done.
   The repo's fuzzer uses texts under 4 KB, whose trees free in one step,
   and the directed test makes no edit between steps.  Here two buffers of
   FILE share release.c's one stack: random edits (small ones, and deletes
   of hundreds of lines so that thousands of nodes are pending), updates
   stopped after a random number of asks, faces read at any moment, and one
   buffer freed and reopened while the other's nodes are pending.  At the
   end each buffer's faces must equal a fresh parse of the same text.
   Built with ASan, UBSan and LeakSanitizer.  usage: stress FILE ROUNDS SEED */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "syntax.h"
#include "text.h"

static unsigned long long rng;
static unsigned rnd(unsigned n) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return (unsigned)(rng % n); }
static int left, stops;
static int stop(void) { if (left > 0 && --left == 0) { stops++; return 1; } return 0; }
static unsigned char f1[1 << 21], f2[1 << 21];
static char b[1 << 21];

static void same_as_fresh(const char *path, Syntax *s, Text *t)
{
	while (syntax_update(s, t, NULL) == 2) ;
	size_t n = text_len(t);
	if (n > sizeof f1) n = sizeof f1;
	memset(f1, 0, n), memset(f2, 0, n);
	syntax_faces(s, 0, n, f1);
	while (syntax_update(s, t, NULL)) ;
	syntax_faces(s, 0, n, f1);
	Syntax *q = syntax_open(path, t);
	syntax_faces(q, 0, n, f2);
	while (syntax_update(q, t, NULL)) ;
	syntax_faces(q, 0, n, f2);
	size_t d = 0;
	for (size_t i = 0; i < n; i++) d += f1[i] != f2[i];
	printf("  faces differing from a fresh parse: %zu of %zu bytes\n", d, n);
	syntax_free(q);
}

int main(int argc, char **argv)
{
	if (argc != 4) return 2;
	FILE *f = fopen(argv[1], "rb");
	if (!f) return 1;
	size_t n = fread(b, 1, sizeof b / 2, f);
	fclose(f);
	int rounds = atoi(argv[2]);
	rng = 2 * strtoull(argv[3], NULL, 10) + 1;
	Text *t[2] = { text_new(b, n), text_new(b, n) };
	Syntax *s[2] = { syntax_open(argv[1], t[0]), syntax_open(argv[1], t[1]) };
	if (!s[0] || !s[1]) return 3;
	int midsteps = 0, frees = 0;
	for (int i = 0; i < rounds; i++) {
		int k = (int)rnd(2);
		size_t len = text_len(t[k]);
		unsigned op = rnd(100);
		if (op < 35) {                        /* a stopped update */
			left = 1 + (int)rnd(6);
			int r = syntax_update(s[k], t[k], stop);
			midsteps += r == 2;
		} else if (op < 55) {                 /* faces of a window */
			size_t a = rnd((unsigned)len), z = a + 4000 < len ? a + 4000 : len;
			syntax_faces(s[k], a, z, f1);
		} else if (op < 85) {                 /* a key */
			size_t pos = rnd((unsigned)len);
			static const char *const ins[] = { "x", "{", "}", "/*", "*/", "\"", "\n", "(", ";", "#if 0\n" };
			const char *x = ins[rnd(10)];
			syntax_edit(s[k], t[k], pos, 0, x, strlen(x));
			if (text_insert(t[k], pos, x, strlen(x))) return 4;
		} else if (op < 95) {                 /* a large delete or paste */
			size_t pos = rnd((unsigned)len), del = rnd(30000);
			if (del > len - 1 - pos) del = len - 1 - pos;
			if (len > n / 2 && rnd(2)) {
				syntax_edit(s[k], t[k], pos, del, "", 0);
				if (text_delete(t[k], pos, del)) return 4;
			} else if (len + 30000 < sizeof f1) {
				size_t from = rnd((unsigned)(n - 30000));
				syntax_edit(s[k], t[k], pos, 0, b + from, 30000);
				if (text_insert(t[k], pos, b + from, 30000)) return 4;
			}
		} else if (op < 97) {                 /* a full update */
			while (syntax_update(s[k], t[k], NULL) == 2) ;
		} else {                              /* close and reopen while the other's nodes may be pending */
			syntax_free(s[k]);
			s[k] = syntax_open(argv[1], t[k]);
			frees++;
		}
	}
	printf("%s seed %s: %d rounds, %d updates stopped, %d reopen\n", argv[1], argv[3], rounds, midsteps, frees);
	same_as_fresh(argv[1], s[0], t[0]);
	same_as_fresh(argv[1], s[1], t[1]);
	syntax_free(s[0]), syntax_free(s[1]);
	text_free(t[0]), text_free(t[1]);
	return 0;
}
