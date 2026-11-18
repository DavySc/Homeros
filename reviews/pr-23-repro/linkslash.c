/* Review of PR 23: a link whose target is written with a trailing '/'
   (ln -s sub/ x).  Renders with "//", and the rendered lines, saved
   unedited, are not a no-op. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/tools/oil.h"
int main(void)
{
	const char *dirs[1] = { "/tmp/ol/" };
	oil_entry(dirs[0], "sub", OIL_DIR, NULL, 0);
	oil_entry(dirs[0], "x", OIL_LINK, "sub/", 1);
	char *r; size_t n;
	oil_render(dirs[0], &r, &n);
	printf("%.*s", (int)n, r);
	const char *text[1] = { r };
	OilAction *act; OilError *err; int nerr;
	int na = oil_actions(1, dirs, text, &n, &act, &err, &nerr);
	printf("%d actions for the unedited buffer\n", na);
	for (int i = 0; i < na; i++) printf("  %c %c %s %s link=%s\n", act[i].type, act[i].etype, act[i].src, act[i].dst ? act[i].dst : "-", act[i].link ? act[i].link : "-");
	if (na > 0) oil_free(act, na);
	free(err); free(r); oil_reset();
	return 0;
}
