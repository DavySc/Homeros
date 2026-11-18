/* Review of PR 23: oil.c's message for the same edit as parent.lua. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/tools/oil.h"
int main(void)
{
	const char *dirs[2] = { "/tmp/op/", "/tmp/op/sub/deep/" };
	oil_entry("/tmp/op/", "sub", OIL_DIR, NULL, 0);
	oil_entry("/tmp/op/sub/", "deep", OIL_DIR, NULL, 0);
	const char *text[2] = { "/000 x  ../\n/001 x  sub/\n", "/000 x  ../\n/001 x  sub/\n" };
	size_t len[2] = { strlen(text[0]), strlen(text[1]) };
	OilAction *act; OilError *err; int nerr;
	int na = oil_actions(2, dirs, text, len, &act, &err, &nerr);
	printf("%d actions\n", na);
	for (int i = 0; i < nerr; i++) printf("error: %s\n", err[i].msg);
	if (na > 0) oil_free(act, na);
	free(err); oil_reset();
	return 0;
}
