/* Reviewer's out-of-memory test, one source for both builds: parse each file
   with the k-th allocation failing, for every k until a parse succeeds.
   Wants: a clean failure (NULL), nothing leaked (LeakSanitizer), no crash,
   and a later parse of the same text still right.  Link with
   -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "text.h"
#include "org.h"
void *__real_malloc(size_t); void *__real_calloc(size_t, size_t); void *__real_realloc(void *, size_t);
static long fail_at = -1, count;
static int hit(void) { return fail_at >= 0 && count++ == fail_at; }
void *__wrap_malloc(size_t n) { return hit() ? NULL : __real_malloc(n); }
void *__wrap_calloc(size_t a, size_t b) { return hit() ? NULL : __real_calloc(a, b); }
void *__wrap_realloc(void *p, size_t n) { return hit() ? NULL : __real_realloc(p, n); }

int main(int argc, char **argv)
{
	long failed = 0;
	for (int i = 1; i < argc; i++) {
		FILE *f = fopen(argv[i], "rb");
		fseek(f, 0, SEEK_END); size_t k = (size_t)ftell(f); rewind(f);
		char *b = __real_malloc(k + 1);
		if (fread(b, 1, k, f) != k) return 2;
		fclose(f);
		Text *t = text_new(b, k);
		size_t want;
#ifdef BUILD_A
		Org *o = org_new(t); want = org_count(o); org_free(o);
		for (long a = 0;; a++) {
			fail_at = a; count = 0;
			o = org_new(t);
			fail_at = -1;
			if (o) { if (org_count(o) != want) { printf("%s: wrong after failures\n", argv[i]); return 1; } org_free(o); break; }
			failed++;
		}
		/* and in org_edit: the list is empty after a failure, right after the next success */
		o = org_new(t);
		for (long a = 0; a < 40; a++) {
			text_insert(t, k / 2, "\n* h\n", 5);
			fail_at = a; count = 0;
			int r = org_edit(o, t, k / 2, 0, 5);
			fail_at = -1;
			text_delete(t, k / 2, 5);
			if (r) { failed++; if (org_count(o)) { printf("%s: list not empty after a failed edit\n", argv[i]); return 1; } org_edit(o, t, 0, 0, text_len(t)); }
			else org_edit(o, t, k / 2, 5, 0);
			if (org_count(o) != want) { printf("%s: %zu nodes after a failed edit and a full one, want %zu\n", argv[i], org_count(o), want); return 1; }
		}
		org_free(o);
#else
		OrgNode *e = org_parse(b, k, &want); free(e);
		for (long a = 0;; a++) {
			size_t got;
			fail_at = a; count = 0;
			e = org_parse(b, k, &got);
			fail_at = -1;
			if (e) { if (got != want) { printf("%s: wrong after failures\n", argv[i]); return 1; } free(e); break; }
			failed++;
		}
#endif
		text_free(t); free(b);
	}
	printf("%ld failed allocations, each a clean failure\n", failed);
	return 0;
}
