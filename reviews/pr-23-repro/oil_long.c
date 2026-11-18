/* Review of PR 23: a new entry whose nested name is longer than oil.c's
   4096-byte url writes past it (oil_actions, the snprintf at url + u). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/tools/oil.h"
int main(int argc, char **argv)
{
	size_t n = argc > 1 ? (size_t)atol(argv[1]) : 5000;
	const char *dirs[1] = { "/tmp/oc/" };
	char *t = malloc(n + 64);
	memset(t, 'a', n);
	strcpy(t + n, "/b/c\n");
	const char *text[1] = { t };
	size_t len[1] = { strlen(t) };
	OilAction *act; OilError *err; int nerr;
	int na = oil_actions(1, dirs, text, len, &act, &err, &nerr);
	printf("%d actions, %d errors\n", na, nerr);
	if (na > 0) oil_free(act, na);
	free(err); free(t);
	return 0;
}
