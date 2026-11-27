/* review, second pass: a file's faces read a line at a time against one read of the whole text; a line's faces must
   not depend on how the range was asked.  usage: ranges FILE... */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "text.h"
#include "tsql.h"
#include "mk.h"
int main(int argc, char **argv)
{
	int bad = 0;
	for (int i = 1; i < argc; i++) {
		FILE *fp = fopen(argv[i], "rb");
		if (!fp) continue;
		fseek(fp, 0, SEEK_END); long n = ftell(fp); rewind(fp);
		char *b = malloc(n + 1); if (fread(b, 1, n, fp) != (size_t)n) return 1;
		fclose(fp);
		unsigned char *f = calloc(1, n + 1), *g = calloc(1, n + 1);
		Text *t = text_new(b, n);
		Tsql *q = mk(t, 3), *h = mk(t, 3);
		while (tsql_update(q, NULL) == 2) ;
		while (tsql_update(h, NULL) == 2) ;
		for (size_t l = 0; l < text_lines(t); l++) {
			size_t a = text_line_start(t, l), z = text_line_start(t, l + 1);
			tsql_faces(q, a, z, f + a);
		}
		tsql_faces(h, 0, n, g);
		size_t d = 0, first = 0;
		for (long k = 0; k < n; k++) if (f[k] != g[k] && b[k] != '\n' && !d++) first = k;
		if (d) bad++, printf("%s: %zu bytes differ, first at %zu (line %zu, col %zu): by lines %d, whole %d\n", argv[i], d, first,
			text_line_of(t, first), first - text_line_start(t, text_line_of(t, first)), f[first], g[first]);
		tsql_free(q), tsql_free(h), text_free(t), free(b), free(f), free(g);
	}
	printf("ranges: %d of %d files differ\n", bad, argc - 1);
	return bad != 0;
}
