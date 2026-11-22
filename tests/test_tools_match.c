/* The picker's matcher against snacks.picker's own, as recorded from snacks
   by src/tools/conform/match.py.  Scoring the recording's cases must give
   the recording back byte for byte.  With "-" this is that script's driver:
   cases on stdin, scored cases on stdout. */
#include <string.h>
#include "check.h"
#include "match.h"

#define REC "src/tools/conform/match-recorded.txt"
enum { LINE = 4096, ITEMS = 64 };

/* Reads cases (P and I lines; F and S lines are skipped) and writes them
   scored: P, the I lines as read, F line col, one S per item. */
static void run(FILE *in, FILE *out)
{
	static char ln[LINE], pat[LINE], it[ITEMS][LINE];
	int n = 0, have = 0;
	for (;;) {
		char *got = fgets(ln, sizeof ln, in);
		if (got) ln[strcspn(ln, "\n")] = 0;
		if (have && (!got || ln[0] == 'P')) {
			Match m;
			m.filename_bonus = 1;
			match_init(&m, pat, strlen(pat));
			fprintf(out, "P\t%s\n", pat);
			for (int i = 0; i < n; i++) fprintf(out, "I\t%s\n", it[i]);
			fprintf(out, "F\t%ld\t%ld\n", m.line, m.col);
			for (int i = 0; i < n; i++) {
				/* "<file>\t<text>", or "\x01\t<text>" for an item with no file */
				char *t = strchr(it[i], '\t') + 1, *f = it[i][0] == 1 ? NULL : it[i];
				fprintf(out, "S\t%d\n", match_score(&m, t, strlen(t), f, f ? (size_t)(t - 1 - f) : 0));
			}
			have = 0;
		}
		if (!got) return;
		if (ln[0] == 'P') { strcpy(pat, ln + 2); n = 0; have = 1; }
		else if (ln[0] == 'I') { CHECK(n < ITEMS); strcpy(it[n++], ln + 2); }
	}
}

int main(int argc, char **argv)
{
	if (argc == 2 && !strcmp(argv[1], "-")) { run(stdin, stdout); return 0; }
	FILE *in = fopen(REC, "r"), *out = tmpfile();
	CHECK(in && out);
	run(in, out);
	rewind(in); rewind(out);
	static char a[LINE], b[LINE];
	int cases = 0, bad = 0;
	char pat[LINE] = "";
	while (fgets(a, sizeof a, in)) {
		CHECK(fgets(b, sizeof b, out));
		if (a[0] == 'P') { cases++; strcpy(pat, a); }
		if (strcmp(a, b) && bad++ < 5) fprintf(stderr, "case %s  snacks %s  ours   %s", pat, a, b);
	}
	CHECK(!fgets(b, sizeof b, out));
	printf("test_tools_match: %d cases from snacks, %d lines differ\n", cases, bad);
	CHECK(cases > 0 && !bad);
	return 0;
}
