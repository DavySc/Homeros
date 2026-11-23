/* random access equals one walk: faces and conceal asked in random order, with
   edits between, against a fresh OrgFx asked in order (checkpoints, look-back) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "text.h"
#include "org.h"
#include "syntax.h"
static char *slurp(const char *p, size_t *n)
{
	FILE *f = fopen(p, "rb"); fseek(f, 0, SEEK_END); *n = ftell(f); rewind(f);
	char *b = malloc(*n + 1); if (fread(b, 1, *n, f) != *n) exit(2); fclose(f); return b;
}
static int same(Conceal *a, size_t na, Conceal *b, size_t nb)
{
	if (na != nb) return 0;
	for (size_t i = 0; i < na; i++) if (a[i].a != b[i].a || a[i].z != b[i].z || strcmp(a[i].s, b[i].s)) return 0;
	return 1;
}
int main(int argc, char **argv)
{
	(void)argc;
	size_t n; char *b = slurp(argv[1], &n); unsigned seed = atoi(argv[2]); srand(seed);
	Text *t = text_new(b, n);
	OrgFx *ref = orgfx_new(t), *x = orgfx_new(t);
	size_t L = text_lines(t), bad = 0;
	unsigned char *fr = malloc(n + 1), *fx = malloc(n + 1);
	orgfx_update(ref, NULL); orgfx_update(x, NULL);
	memset(fr, 0, n); orgfx_faces(ref, 0, n, fr);
	for (int k = 0; k < 300; k++) {   /* random windows, sometimes a conceal in between */
		size_t a = (size_t)rand() % n, z = a + 1 + (size_t)rand() % 4000; if (z > n) z = n;
		if (rand() % 3 == 0) { Conceal c[64]; orgfx_conceal(x, (size_t)rand() % (L + 1), c, 64); }
		memset(fx + a, 0, z - a); orgfx_faces(x, a, z, fx + a);
		if (memcmp(fx + a, fr + a, z - a)) {
			size_t i = a; while (fx[i] == fr[i]) i++;
			if (bad++ < 5) printf("faces differ at byte %zu (line start %zu): %d vs %d\n", i, i, fx[i], fr[i]);
		}
	}
	/* conceal: reference line by line in a fresh OrgFx */
	OrgFx *r2 = orgfx_new(t), *x2 = orgfx_new(t);
	for (int k = 0; k < 300; k++) {
		size_t l = (size_t)rand() % (L + 1);
		Conceal c1[64], c2[64];
		if (rand() % 3 == 0) { unsigned char tmp[64]; size_t a = (size_t)rand() % n; orgfx_faces(x2, a, a + 1 <= n ? a + 1 : n, tmp); }
		size_t k2 = orgfx_conceal(x2, l, c2, 64);
		OrgFx *fresh = orgfx_new(t);
		for (size_t m = 0; m + 1 < l; m += 1 + l / 3) { Conceal cc[64]; orgfx_conceal(fresh, m, cc, 64); }   /* walk toward it */
		size_t k1 = orgfx_conceal(fresh, l, c1, 64);
		orgfx_free(fresh);
		if (!same(c1, k1, c2, k2) && bad++ < 10) printf("conceal differs on line %zu: %zu vs %zu spans\n", l, k2, k1);
	}
	(void)r2;
	printf("%s seed %u: %zu differ\n", argv[1], seed, bad);
	return bad != 0;
}
