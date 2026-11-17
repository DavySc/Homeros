/* Review of PR 30: what one release step costs, and what is still freed in
   one go.  Parses FILE, deletes all of it but the last byte, reparses, then
   frees the old tree through syntax_update with a stop that records the
   time between its asks (each is one RELEASE_STEP of 1,024 nodes).  Then
   parses FILE again and times syntax_free, which frees without asking.
   usage: freetime FILE */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "syntax.h"
#include "text.h"

static double now(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}
static double last, gap, sum;
static int asks;
static int stop(void)
{
	double n = now();
	if (n - last > gap) gap = n - last;
	sum += n - last;
	last = n;
	asks++;
	return 0;
}

int main(int argc, char **argv)
{
	if (argc != 2) return 2;
	FILE *f = fopen(argv[1], "rb");
	if (!f) return 1;
	static char b[1 << 21];
	size_t n = fread(b, 1, sizeof b, f);
	fclose(f);
	static unsigned char fc[4096];
	Text *t = text_new(b, n);
	Syntax *s = syntax_open(argv[1], t);
	if (!s) return 3;
	syntax_faces(s, 0, 4000, fc);
	while (syntax_update(s, t, NULL)) ;
	syntax_edit(s, t, 0, n - 1, "", 0);
	if (text_delete(t, 0, n - 1)) return 4;
	syntax_faces(s, 0, 1, fc);
	while (syntax_update(s, t, NULL) == 2) ;       /* the reparse; the old tree is now pending */
	last = now();
	while (syntax_update(s, t, stop)) ;
	stop();
	printf("%s: old tree freed in %d steps, %.3f ms in all, longest step %.3f ms\n", argv[1], asks, sum, gap);
	syntax_free(s);
	text_free(t);
	t = text_new(b, n);
	s = syntax_open(argv[1], t);
	syntax_faces(s, 0, 4000, fc);
	while (syntax_update(s, t, NULL)) ;
	double a = now();
	syntax_free(s);
	printf("%s: syntax_free of the whole tree: %.3f ms without a stop ask\n", argv[1], now() - a);
	text_free(t);
	return 0;
}
