/* formats each file named on the command line, whole and over one range; prints result lengths and bytes */
#include <stdio.h>
#include <stdlib.h>
#include "text.h"
#include "sqlfmt.h"
int main(int argc, char **argv)
{
	for (int i = 1; i < argc; i++) {
		FILE *f = fopen(argv[i], "rb"); if (!f) return 2;
		static char buf[1 << 24]; size_t n = fread(buf, 1, sizeof buf, f); fclose(f);
		for (int r = 0; r < 2; r++) {
			Text *t = text_new(buf, n);
			size_t a = r ? n / 3 : 0, z = r ? 2 * n / 3 : n, m; const char *res;
			int k = sqlfmt(t, &a, &z, &res, &m);
			printf("%s %d kept=%d a=%zu z=%zu n=%zu\n", argv[i], r, k, a, z, k < 0 ? 0 : m);
			if (k >= 0) fwrite(res, 1, m, stdout);
			putchar('\n');
			text_free(t);
		}
	}
	return 0;
}
