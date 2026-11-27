/* scale PRE W LEN...: least CPU ms of 5 reads of a Lean line of PRE then W
   repeated to LEN bytes, for each LEN (review of PR 174) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "syntax.h"
#include "text.h"
static char text[1 << 22];
static unsigned char got[1 << 22];
static double read_ms(size_t n)
{
	clock_t c0 = clock();
	Text *t = text_new(text, n);
	Syntax *s = syntax_open("x.lean", t);
	size_t r1, r2;
	while (syntax_update(s, t, NULL, &r1, &r2) == 2) ;
	syntax_faces(s, 0, n, got);
	syntax_free(s), text_free(t);
	return (double)(clock() - c0) * 1e3 / CLOCKS_PER_SEC;
}
int main(int argc, char **argv)
{
	double prev = 0;
	for (int a = 3; a < argc; a++) {
		size_t len = strtoul(argv[a], 0, 10), n = (size_t)sprintf(text, "%s", argv[1]);
		for (size_t e = n + len; n < e; ) n += (size_t)sprintf(text + n, "%s", argv[2]);
		text[n++] = '\n';
		double best = 1e9;
		for (int k = 0; k < 5; k++) { double ms = read_ms(n); if (ms < best) best = ms; }
		printf("%8zu %9.3f ms %6.2f\n", len, best, prev ? best / prev : 0);
		prev = best;
	}
	return 0;
}
