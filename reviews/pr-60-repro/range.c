/* ranges: formatting [0,a), then [a,z), then [z,end) one after the other
   must give what formatting the whole text gives (each token's gap and
   case belong to exactly one of the three), and each edit must stay
   inside its range (from the end of the token before it).
   usage: range FILE ROUNDS SEED */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "text.h"
#include "sqlfmt.h"
static char buf[1 << 23], cur[1 << 23], whole[1 << 23], tmp[1 << 23];
static size_t apply(char *s, size_t n, size_t a, size_t z, int *kept, size_t *ea, size_t *ez, size_t *em)
{
	Text *t = text_new(s, n);
	const char *r; size_t m;
	*kept = sqlfmt(t, &a, &z, &r, &m);
	if (*kept < 0 || a > z || z > n) { printf("bad edit\n"); exit(1); }
	memcpy(tmp, s, a); memcpy(tmp + a, r, m); memcpy(tmp + a + m, s + z, n - z);
	memcpy(s, tmp, n - (z - a) + m);
	text_free(t);
	*ea = a, *ez = z, *em = m;
	return n - (z - a) + m;
}
int main(int argc, char **argv)
{
	FILE *f = fopen(argv[1], "rb");
	size_t n = fread(buf, 1, sizeof buf, f), ea, ez, em;
	int k, bad = 0, rounds = atoi(argv[2]);
	srand((unsigned)atoi(argv[3]));
	memcpy(whole, buf, n);
	size_t wn = apply(whole, n, 0, n, &k, &ea, &ez, &em);
	if (k) { printf("kept %d: skipped\n", k); return 0; }
	for (int i = 0; i < rounds; i++) {
		size_t a = (size_t)rand() % (n + 1), z = a + (size_t)rand() % (n + 1 - a);
		if (rand() % 2) z = a + (size_t)rand() % 200 < n ? a + (size_t)rand() % 200 : n;
		memcpy(cur, buf, n);
		/* back to front, so a and z stay offsets of the text they are applied to */
		size_t cn = apply(cur, n, z, n, &k, &ea, &ez, &em);
		if (ez > n) bad++, printf("%zu %zu: tail edit ends past the text\n", a, z);
		size_t before = cn;
		cn = apply(cur, cn, a, z, &k, &ea, &ez, &em);
		if (ez > z) bad++, printf("%zu %zu: edit [%zu,%zu) runs past z\n", a, z, ea, ez);
		(void)before;
		cn = apply(cur, cn, 0, a, &k, &ea, &ez, &em);
		if (ez > a) bad++, printf("%zu: head edit [%zu,%zu) runs past a\n", a, ea, ez);
		if (cn != wn || memcmp(cur, whole, wn)) bad++, printf("%zu %zu: three ranges differ from the whole\n", a, z);
	}
	printf("%d rounds, %d bad\n", rounds, bad);
	return bad != 0;
}
