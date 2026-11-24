/* review probe: fail the N-th allocation (malloc, calloc, realloc) and see
   what sqlfmt_units returns, and whether the popup's collect survives.
   Linked with -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "check.h"
#include "core.h"
#include "snip.h"
#include "sqlfmt.h"
#include "tools.h"
void *__real_malloc(size_t), *__real_calloc(size_t, size_t), *__real_realloc(void *, size_t);
static long cnt, failat = -1, from = -1;
static int hit(void) { cnt++; return cnt == failat || (from >= 0 && cnt >= from); }
void *__wrap_malloc(size_t n) { return hit() ? NULL : __real_malloc(n); }
void *__wrap_calloc(size_t a, size_t b) { return hit() ? NULL : __real_calloc(a, b); }
void *__wrap_realloc(void *p, size_t n) { return hit() ? NULL : __real_realloc(p, n); }
static void feed(Editor *e, int k) { if (e->ks.pending || !snip_key(e, k, 0)) keys_feed(e, k, 0); }
int main(int argc, char **argv)
{
	static char t[4 << 20];
	FILE *f = fopen(argv[1], "rb");
	CHECK(f);
	size_t n = fread(t, 1, sizeof t - 1, f);
	fclose(f);
	if (argc > 2 && !strcmp(argv[2], "units")) {
		Text *tx = text_new(t, n);
		CHECK(tx);
		size_t (*u)[2];
		if (argc > 3) { cnt = 0; failat = atol(argv[3]); if (argc > 4) from = failat, failat = -1; size_t r = sqlfmt_units(tx, &u); printf("%zu\n", r); return 0; }
		failat = from = -1, cnt = 0;
		size_t k = sqlfmt_units(tx, &u);
		long allocs = cnt;
		printf("full: %zu units, %ld allocations\n", k, allocs);
		int zero = 0, full = 0, part = 0;
		for (int mode = 0; mode < 2; mode++) {
			zero = full = part = 0;
			for (long i = 1; i <= allocs; i++) {
				cnt = 0, failat = mode ? -1 : i, from = mode ? i : -1;
				size_t r = sqlfmt_units(tx, &u);
				failat = from = -1;
				if (!r) zero++; else if (r == k) full++; else { part++; if (part <= 5) printf("  %s allocation %ld: %zu of %zu units\n", mode ? "from" : "only", i, r, k); }
			}
			printf("%s: 0 units %d, all units %d, some units %d\n", mode ? "every allocation from the N-th fails" : "only the N-th fails", zero, full, part);
		}
		return 0;
	}
	/* popup: argv[2] = buffer name, argv[3] = key, argv[4] = N, argv[5] = 0 only / 1 from */
	static Editor e;
	char dir[] = "/tmp/zp141-XXXXXX", path[256], st[64];
	CHECK(mkdtemp(dir));
	snprintf(st, sizeof st, "%s/state", dir); setenv("XDG_STATE_HOME", st, 1);
	snprintf(path, sizeof path, "%s/%s", dir, argv[2]);
	char *bar = strchr(t, '|');
	CHECK(bar && !core_open(&e, path));
	CHECK(!core_edit(&e, 0, 0, t, (size_t)(bar - t)) && !core_edit(&e, (size_t)(bar - t), 0, bar + 1, strlen(bar + 1)));
	e.win->line = text_line_of(e.buf->text, (size_t)(bar - t));
	e.win->col = (size_t)(bar - t) - core_line_start(&e, e.win->line) - 1;
	keys_feed(&e, 'a', 0);
	if (!getenv("NOWARM")) feed(&e, 'x'), feed(&e, KEY_BS);   /* the journal's base and first-use buffers, outside the count */
	long N = atol(argv[4]);
	cnt = 0;
	if (atoi(argv[5])) from = N; else failat = N;
	for (const char *k = argv[3]; *k; k++) feed(&e, *k);
	failat = from = -1;
	printf("allocations %ld, npum %d\n", cnt, e.npum);
	return 0;
}
