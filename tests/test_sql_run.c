/* Package 22's GO splitter against sqlcmd 18: replays the scripts
   tests/test_sql_run.py ran through sqlcmd (tests/test_sql_run.txt) and
   compares the batches sql_split gives, each repeated its count, with the
   batches the server saw, and the GO line sqlcmd refused.  sqlcmd reads
   its file as lines, so CRs are left out of the comparison: the editor
   sends a buffer's bytes as they are, as SSMS does. */
#include <string.h>
#include "check.h"
#include "../src/sql/run.h"

static size_t nocr(char *s, size_t n)
{
	size_t m = 0;
	for (size_t i = 0; i < n; i++) if (s[i] != '\r') s[m++] = s[i];
	return m;
}

int main(void)
{
	FILE *f = fopen("tests/test_sql_run.txt", "rb");
	CHECK(f);
	static char s[65536], w[65536], q[65536];
	size_t n, m, nb, cases = 0, bad = 0;
	long want;
	SqlBatches b = {0};
	while (fscanf(f, "#script %zu", &n) == 1) {
		/* each count ends at its '\n': a format's "\n" would eat a script's leading blanks */
		CHECK(fgetc(f) == '\n' && n < sizeof s && fread(s, 1, n, f) == n && fscanf(f, "\n#batches %zu %ld", &nb, &want) == 2 && fgetc(f) == '\n');
		Text *x = text_new(s, n);
		long got = sql_split(x, 0, n, &b);
		CHECK(x && got >= 0);
		size_t i = 0;
		long rep = 0;
		int same = got == want;
		for (size_t j = 0; j < nb; j++) {
			CHECK(fscanf(f, "#b %zu", &m) == 1 && fgetc(f) == '\n' && m < sizeof w && fread(w, 1, m, f) == m && fgetc(f) == '\n');
			while (i < b.n && rep == b.b[i].count) i++, rep = 0;
			if (i == b.n) { same = 0; continue; }
			size_t qn = text_read(x, b.b[i].a, q, b.b[i].z - b.b[i].a);
			same &= nocr(q, qn) == nocr(w, m) && !memcmp(q, w, nocr(w, m));
			rep++;
		}
		while (i < b.n && rep == b.b[i].count) i++, rep = 0;
		same &= i == b.n;
		cases++;
		if (!same && bad++ < 5) {
			fprintf(stderr, "script:\n%.*s\nsqlcmd: %zu batches, refused line %ld; sql_split: %zu batches, refused line %ld\n", (int)n, s, nb, want, b.n, got);
			for (size_t j = 0; j < b.n; j++) {
				size_t qn = text_read(x, b.b[j].a, q, b.b[j].z - b.b[j].a);
				fprintf(stderr, "  x%ld [%.*s]\n", b.b[j].count, (int)qn, q);
			}
		}
		text_free(x);
	}
	free(b.b);
	fclose(f);
	printf("sqlcmd's GO: %zu scripts, %zu differ\n", cases, bad);
	CHECK(cases && !bad);
	return 0;
}
