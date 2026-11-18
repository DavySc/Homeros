/* libFuzzer over the matcher (from the review of PR 23).  Input: prompt
   NUL text; the model is the sanitizers (the scores are checked against
   snacks by test_tools_match). */
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "../src/tools/match.h"
int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n)
{
	const uint8_t *z = memchr(d, 0, n);
	size_t pn = z ? (size_t)(z - d) : n, tn = z ? n - pn - 1 : 0;
	char *p = malloc(pn + 1), *t = malloc(tn + 1);   /* exact sizes, so ASan sees an overread */
	memcpy(p, d, pn);
	if (tn) memcpy(t, z + 1, tn);
	Match m;
	m.filename_bonus = 1;
	match_init(&m, p, pn);
	match_score(&m, t, tn, t, tn);
	match_score(&m, t, tn, NULL, 0);
	free(p); free(t);
	return 0;
}
