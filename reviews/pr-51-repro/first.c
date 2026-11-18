/* Review of PR 51: what the first use of a language costs.  Opens a one-line
   file of the type argv[1] names (c, lua, md, sql), and times syntax_open,
   the first syntax_faces and the first syntax_update, with the longest
   stretch in which the update does not ask stop.  Prints the resident set
   after each (VmRSS). */
#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "syntax.h"
#include "text.h"
static double now(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6; }
static double last, gap;
static int ask(void) { double t = now(); if (t - last > gap) gap = t - last; last = t; return 0; }
static long rss(void)
{
	FILE *f = fopen("/proc/self/statm", "r");
	long a = 0, b = 0;
	if (f) { if (fscanf(f, "%ld %ld", &a, &b) != 2) b = 0; fclose(f); }
	return b * 4;
}
int main(int argc, char **argv)
{
	if (argc < 2) return 2;
	char path[64];
	snprintf(path, sizeof path, "x.%s", argv[1]);
	const char *b = "x\n";
	static unsigned char f[16];
	Text *t = text_new(b, strlen(b));
	long r0 = rss();
	double t0 = now();
	Syntax *s = syntax_open(path, t);
	if (!s) return 2;
	double t1 = now();
	syntax_faces(s, 0, 2, f);
	double t2 = last = now();
	while (syntax_update(s, t, ask)) ;
	ask();
	double t3 = now();
	long r1 = rss();
	syntax_faces(s, 0, 2, f);
	/* anonymous (heap) and file-backed (the binary's tables) parts */
	FILE *st = fopen("/proc/self/status", "r");
	char line[256];
	long an = 0, fi = 0;
	while (st && fgets(line, sizeof line, st)) sscanf(line, "RssAnon: %ld", &an), sscanf(line, "RssFile: %ld", &fi);
	if (st) fclose(st);
	printf("%-4s open %.2f ms, first faces %.2f ms, first update %.2f ms (longest stretch without asking stop %.2f ms); RSS %ld KB before, %ld KB after (%ld heap, of which %zu KB in use; %ld of the binary's pages)\n",
	       argv[1], t1 - t0, t2 - t1, t3 - t2, gap, r0, r1, an, mallinfo2().uordblks / 1024, fi);
	/* what glibc gives back when asked (the query compile's working memory) */
	malloc_trim(0);
	printf("     after malloc_trim(0): RSS %ld KB\n", rss());
	return 0;
}
