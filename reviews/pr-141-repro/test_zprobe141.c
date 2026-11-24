/* review probe: FILE holds SQL with one '|' (the cursor); KEYS are typed there.
   Prints the popup.  -t N: time N word starts ("s" typed, Esc, undo). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "check.h"
#include "core.h"
#include "snip.h"
#include "tools.h"
static void feed(Editor *e, int k) { if (e->ks.pending || !snip_key(e, k, 0)) keys_feed(e, k, 0); }
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }
int main(int argc, char **argv)
{
	static Editor e;
	if (argc < 4) return 2;
	FILE *f = fopen(argv[1], "rb");
	CHECK(f);
	static char t[4 << 20];
	size_t n = fread(t, 1, sizeof t - 1, f);
	t[n] = 0;
	fclose(f);
	char *bar = strchr(t, '|');
	CHECK(bar);
	char dir[] = "/tmp/zp141-XXXXXX", path[256], st[64];
	CHECK(mkdtemp(dir));
	snprintf(st, sizeof st, "%s/state", dir); setenv("XDG_STATE_HOME", st, 1); setenv("XDG_DATA_HOME", st, 1);
	snprintf(path, sizeof path, "%s/%s", dir, argv[2]);
	CHECK(!core_open(&e, path));
	CHECK(!core_edit(&e, 0, 0, t, (size_t)(bar - t)) && !core_edit(&e, (size_t)(bar - t), 0, bar + 1, strlen(bar + 1)));
	e.win->line = text_line_of(e.buf->text, (size_t)(bar - t));
	size_t col = (size_t)(bar - t) - core_line_start(&e, e.win->line);
	if (col) e.win->col = col - 1, keys_feed(&e, 'a', 0); else e.win->col = 0, keys_feed(&e, 'i', 0);
	const char *k = argv[3];
	if (!strcmp(k, "-t")) {
		int reps = atoi(argv[4]);
		const char *w = argc > 5 ? argv[5] : "s";
		for (int r = 0; r < reps; r++) {
			double best = 0;
			for (const char *p = w; *p; p++) { double a = now(); feed(&e, *p); double d = now() - a; printf("%c %.3f ms  ", *p, d); (void)best; }
			printf("npum %d\n", e.npum);
			for (const char *p = w; *p; p++) feed(&e, KEY_BS);
			feed(&e, ' '); feed(&e, KEY_BS);
		}
		return 0;
	}
	for (; *k; k++) feed(&e, *k == '~' ? 25 : *k == '^' ? KEY_BS : *k == '$' ? KEY_ESC : *k);
	printf("%d items:", e.npum);
	for (int i = 0; i < e.npum && i < (getenv("MAXI") ? atoi(getenv("MAXI")) : 25); i++) printf(" %s[%c]", e.pum[i].word, e.pum[i].kind[0]);
	puts("");
	size_t len; const unsigned char *l = core_line(&e, e.win->line, &len);
	printf("line: %.*s\n", (int)len, l);
	printf("bytes %zu\n", text_len(e.buf->text));
	return 0;
}
