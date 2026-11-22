/* PR 93 review: does org_todo_step's answer depend on what an earlier call
   left in its buffer?  Each text is stepped once in a fresh state and once
   after a call on 1 MB of one byte (which fills the buffer past the text's
   end); the answers must be equal.  stale FILE...  (FILE.cmd as org_drv -t) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "org.h"
static char big[1 << 13], b[1 << 16], r0[1 << 17];
int main(int argc, char **argv)
{
	int bad = 0;
	for (int i = 1; i < argc; i++) {
		FILE *f = fopen(argv[i], "rb");
		size_t n = fread(b, 1, sizeof b, f), pos, a, z, len, a0 = 0, z0 = 0, l0 = 0;
		fclose(f);
		char path[4096], dirs[4096];
		snprintf(path, sizeof path, "%s.cmd", argv[i]);
		f = fopen(path, "rb");
		if (fscanf(f, "%zu %4000s", &pos, dirs) != 2) return 2;
		fclose(f);
		size_t full = n, p0 = pos; for (n = 0; n <= full; n++) { pos = p0 < n ? p0 : n;
		for (int d = -1; d <= 1; d += 2)
			for (const char *x = "\0 *+>]:\n-0\tT<["; ; x++) {
				memset(big, *x, sizeof big);
				org_todo_step(big, sizeof big, sizeof big, 1, 29918490, &a, &z, &len);
				const char *r = org_todo_step(b, n, pos, d, 29918490, &a, &z, &len);
				if (!*x) { a0 = a, z0 = z, l0 = len; memcpy(r0, r, len); }
				else if (a != a0 || z != z0 || len != l0 || memcmp(r, r0, len)) {
					printf("%s prefix %zu dir %d: differs after a buffer of 0x%02x\n", argv[i], n, d, *x); bad++;
					break;
				}
				if (x[0] == 0x5b) break;
			}
		}
	}
	printf("%d differ of %d\n", bad, argc - 1);
	return bad != 0;
}
