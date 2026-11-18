/* org_edit with the a-th allocation failing, for every a until it succeeds:
   it must return -1 with the list freed and NULL (LeakSanitizer sees a
   leak, AddressSanitizer a double free), and the first success must equal
   a whole parse.  Six edits per file, among them one that needs a larger
   list, one that makes the span grow and one that forces a whole reparse
   (a babel call's bracket closed by the edit).
   Link with -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc.  oom FILE... */
#include "common.h"
void *__real_malloc(size_t); void *__real_calloc(size_t, size_t); void *__real_realloc(void *, size_t);
static long fail_at = -1, count;
static int hit(void) { return fail_at >= 0 && count++ == fail_at; }
void *__wrap_malloc(size_t n) { return hit() ? NULL : __real_malloc(n); }
void *__wrap_calloc(size_t a, size_t b) { return hit() ? NULL : __real_calloc(a, b); }
void *__wrap_realloc(void *p, size_t n) { return hit() ? NULL : __real_realloc(p, n); }
int main(int argc, char **argv)
{
	long failed = 0, done = 0;
	static const struct { int where; size_t del; const char *ins; } ed[] = {
		{ 2, 0, "x" }, { 2, 0, "\n* h\n** i\n- a\n- b\n| c |\n#+NAME: n\n: f\n\n\n* j\n" }, { 2, 100, "" },
		{ 0, 0, "* top\n" }, { 4, 0, ")" }, { 3, 2000, "\n" } };
	for (int i = 1; i < argc; i++) {
		FILE *fp = fopen(argv[i], "rb");
		fseek(fp, 0, SEEK_END); size_t n = (size_t)ftell(fp); rewind(fp);
		char *s = __real_malloc(n + 64), *t = __real_malloc(n + 128);
		static const char call[] = "#+CALL: f(\n";
		memcpy(s, call, sizeof call - 1);            /* an open babel call at the top */
		if (fread(s + sizeof call - 1, 1, n, fp) != n) return 2;
		fclose(fp);
		n += sizeof call - 1;
		size_t k0, m;
		OrgNode *e0 = org_parse(s, n, &k0);
		for (size_t j = 0; j < sizeof ed / sizeof *ed; j++) {
			size_t pos = ed[j].where * n / 4, del = ed[j].del, ins = strlen(ed[j].ins);
			if (del > n - pos) del = n - pos;
			memcpy(t, s, pos); memcpy(t + pos, ed[j].ins, ins); memcpy(t + pos + ins, s + pos + del, n - pos - del);
			OrgNode *f = org_parse(t, n - del + ins, &m);
			for (long a = 0;; a++) {
				size_t k = k0;
				OrgNode *e = __real_malloc((k0 ? k0 : 1) * sizeof *e);
				memcpy(e, e0, k0 * sizeof *e);
				fail_at = a; count = 0;
				int r = org_edit(&e, &k, t, n - del + ins, pos, del, ins);
				fail_at = -1;
				if (r) { failed++; if (r != -1 || e) { printf("%s edit %zu alloc %ld: returned %d, list %p\n", argv[i], j, a, r, (void *)e); return 1; } continue; }
				if (differ(e, k, f, m) != ORG_NONE) { printf("%s edit %zu: differs after %ld failures\n", argv[i], j, a); return 1; }
				free(e); done++;
				break;
			}
			free(f);
		}
		free(e0); free(s); free(t);
	}
	printf("%ld edits, %ld failed allocations, each -1 with the list freed\n", done, failed);
	return 0;
}
