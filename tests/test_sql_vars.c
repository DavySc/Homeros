/* Package 22's undeclared @variables against SQL Server: replays the
   batches tests/test_sql_vars.py asked the server about
   (tests/test_sql_vars.txt) and compares the variables sql_vars lists
   with the ones the server said each lacks, case ignored, in any order. */
#include <string.h>
#include <strings.h>
#include "check.h"
#include "../src/sql/run.h"

int main(void)
{
	FILE *f = fopen("tests/test_sql_vars.txt", "rb");
	CHECK(f);
	static char s[65536], want[4096];
	size_t n, cases = 0, bad = 0;
	Str got = {0};
	while (fscanf(f, "#batch %zu", &n) == 1) {
		CHECK(fgetc(f) == '\n' && n < sizeof s && fread(s, 1, n, f) == n && fgetc(f) == '\n' && fgets(want, sizeof want, f) && !strncmp(want, "#vars", 5));
		Text *x = text_new(s, n);
		CHECK(x && !sql_vars(x, 0, n, &got, NULL));
		size_t k = 0, m = 0;
		for (size_t i = 0; i < got.n; i += strlen(got.s + i) + 1, k++) {
			size_t l = strlen(got.s + i);
			for (char *p = want + 5; (p = strchr(p, '@')); p++) m += !strncasecmp(p, got.s + i, l) && (p[l] == ' ' || p[l] == '\n');
		}
		size_t nw = 0;
		for (char *p = want + 5; (p = strchr(p, '@')); p++) nw++;
		if (m != k || k != nw) {
			bad++;
			fprintf(stderr, "test_sql_vars: %.*s\n  server: %s  sql_vars:", (int)n, s, want + 6);
			for (size_t i = 0; i < got.n; i += strlen(got.s + i) + 1) fprintf(stderr, " %s", got.s + i);
			fprintf(stderr, "\n");
		}
		text_free(x);
		cases++;
	}
	free(got.s);
	fprintf(stderr, "test_sql_vars: %zu batches, %zu differ\n", cases, bad);
	CHECK(cases && !bad);
	return 0;
}
