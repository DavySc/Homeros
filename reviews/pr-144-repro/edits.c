/* review: small edits typed into slices of real Lean files; the window around each edit, read through the kept index, against a fresh index */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "text.h"
#include "tsql.h"
#include "mk.h"
static unsigned long long s = 88172645463325252ull;
static unsigned rnd(unsigned n) { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return (unsigned)(s % n); }
static const char *const ins[] = { "\n", " ", "-", "/", "/-", "-/", "--", "\"", "'", "`", "*", "_", "#", "{", "}", "[", "]", "(", ")", ":", "d", "e", "f", "def ", "instance", "deriving ",
	"attribute ", "@[", "s!", "\\", "1", ".", "e", "+", "x", "\xc2\xab", "\xc2\xbb", ">", "<", "=", "```", "theorem", "where", "with", "0x", "\xce\xb1", "\xc2\xa0", "!", "?" };
enum { MAX = 1 << 16 };
static unsigned char f[MAX + 4096], g[MAX + 4096];
static int stop_rnd(void) { return !rnd(3); }
int main(int argc, char **argv)
{
	char path[4096];
	unsigned long edits = 0, windows = 0, bad = 0;
	int per = argc > 1 ? atoi(argv[1]) : 100;
	if (argc > 2) s = strtoull(argv[2], NULL, 0);
	while (fgets(path, sizeof path, stdin)) {
		path[strcspn(path, "\n")] = 0;
		FILE *fp = fopen(path, "rb");
		if (!fp) continue;
		static char b[1 << 20];
		size_t n = fread(b, 1, sizeof b, fp), off = n > MAX / 2 ? rnd((unsigned)(n - MAX / 2)) : 0;
		fclose(fp);
		if (n > MAX / 2) n = MAX / 2;
		Text *t = text_new(b + off, n);
		Tsql *q = mk(t, 3);
		while (tsql_update(q, NULL) == 2) ;
		for (int e = 0; e < per; e++, edits++) {
			n = text_len(t);
			size_t p = rnd((unsigned)n + 1), d = rnd(3) ? 0 : 1 + rnd(3);
			const char *w = rnd(4) ? ins[rnd(sizeof ins / sizeof *ins)] : "";
			size_t ni = strlen(w);
			if (d > n - p) d = n - p;
			if (n - d + ni > MAX) continue;
			tsql_edit(q, t, p, d, ni);
			if (text_delete(t, p, d) || text_insert(t, p, w, ni)) return 2;
			n = text_len(t);
			if (rnd(2)) tsql_update(q, stop_rnd);
			/* the screen around the edit, or somewhere else */
			size_t l = text_line_of(t, p), l0 = rnd(4) ? (l > 25 ? l - rnd(26) : 0) : rnd((unsigned)text_lines(t) + 1);
			size_t wa = text_line_start(t, l0), z = text_line_start(t, l0 + 50);
			for (int k = 0; k < 2; k++) {
				memset(f + wa, 0, z - wa);
				for (size_t a = wa, e2; a < z; a = e2) {
					e2 = text_line_start(t, text_line_of(t, a) + 1);
					tsql_faces(q, a, e2, f + a);
				}
				int r;
				while ((r = tsql_update(q, stop_rnd)) == 2) ;
				if (r != 1) break;
			}
			Tsql *h = mk(t, 3);
			while (tsql_update(h, NULL) == 2) ;
			memset(g + wa, 0, z - wa);
			tsql_faces(h, wa, z, g + wa);
			tsql_free(h);
			windows++;
			if (memcmp(f + wa, g + wa, z - wa)) {
				size_t i = wa;
				while (f[i] == g[i]) i++;
				if (bad++ < 5) fprintf(stderr, "%s off %zu edit %d at %zu (-%zu +'%s'): byte %zu face %d, fresh %d\n", path, off, e, p, d, w, i, f[i], g[i]);
			}
		}
		tsql_free(q), text_free(t);
	}
	printf("edits: %lu edits, %lu windows, %lu differ from a fresh index\n", edits, windows, bad);
	return bad != 0;
}
