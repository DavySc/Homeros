/* libFuzzer over oil_actions and oil_render (from the review of PR 23): a
   small fixed cache; the input is the edited text of the two buffers,
   split at the first NUL.  The model is the sanitizers. */
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "../src/tools/oil.h"
int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n)
{
	static const char *dirs[2] = { "/tmp/oc/", "/tmp/oc/sub/" };
	oil_reset();
	oil_entry(dirs[0], "a.c", OIL_FILE, NULL, 0);
	oil_entry(dirs[0], "sub", OIL_DIR, NULL, 0);
	oil_entry(dirs[0], "ln", OIL_LINK, "a.c", 0);
	oil_entry(dirs[1], "b", OIL_FILE, NULL, 0);
	oil_entry(dirs[1], "d", OIL_DIR, NULL, 0);
	const uint8_t *z = memchr(d, 0, n);
	size_t an = z ? (size_t)(z - d) : n, bn = z ? n - an - 1 : 0;
	char *a = malloc(an + 1), *b = malloc(bn + 1);
	memcpy(a, d, an);
	if (bn) memcpy(b, z + 1, bn);
	const char *t[2] = { a, b };
	size_t len[2] = { an, bn };
	OilAction *act; OilError *err; int nerr;
	int na = oil_actions(2, dirs, t, len, &act, &err, &nerr);
	if (na > 0) oil_free(act, na);
	for (int i = 0; i < 2; i++) {
		char *r;
		size_t rn;
		if (!oil_render(dirs[i], &r, &rn)) free(r);
	}
	free(err);
	free(a); free(b);
	return 0;
}
