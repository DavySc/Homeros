/* json_parse alone on a tokens answer of LINES x WORDS tokens: jp LINES WORDS */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../pr148/src/lsp/json.h"
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }
int main(int argc, char **argv)
{
	(void)argc;
	int lines = atoi(argv[1]), words = atoi(argv[2]);
	size_t cap = (size_t)lines * words * 12 + 100, n = 0;
	char *m = malloc(cap);
	n += sprintf(m, "{\"jsonrpc\":\"2.0\",\"id\":7,\"result\":{\"data\":[");
	for (int l = 0; l < lines; l++) for (int w = 0; w < words; w++)
		n += sprintf(m + n, "%s%d,%d,1,%d,0", l || w ? "," : "", w == 0 && l ? 1 : 0, w ? 2 : 0, w % 2);
	n += sprintf(m + n, "]}}");
	Jtok *tk = NULL; int ntk = 0, k = 0;
	double best = 1e9;
	for (int r = 0; r < 20; r++) { double a = now(); if ((k = json_parse(m, n, &tk, &ntk)) < 0) return 1; a = now() - a; if (a < best) best = a; }
	printf("%zu bytes, %d JSON tokens: json_parse %.2f ms (best of 20)\n", n, k, best);
	return 0;
}
