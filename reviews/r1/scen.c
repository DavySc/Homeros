/* Reviewer's scenarios through text.h, one per run: scen FILE NAME.
   Instructions are counted inside run() only (callgrind --toggle-collect=run). */
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "text.h"
static unsigned long long rng = 88172645463325252ull;
static unsigned long long nx(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return rng; }
static double now(void) { struct timespec s; clock_gettime(CLOCK_MONOTONIC, &s); return s.tv_sec + s.tv_nsec / 1e9; }
static char *orig, *buf; static size_t n, sink;
__attribute__((noinline)) static void run(Text **tp, const char *w)
{
	Text *t = *tp;
	size_t len = text_len(t), mid = len / 2;
	if (!strcmp(w, "open")) { *tp = text_new(orig, n); }
	else if (!strcmp(w, "type")) { for (int i = 0; i < 100000; i++) text_insert(t, mid + i, i % 60 == 59 ? "\n" : "a", 1); }
	else if (!strcmp(w, "backspace")) { for (int i = 0; i < 100000; i++) text_delete(t, mid - 1 - i, 1); }
	else if (!strcmp(w, "paste")) { for (int i = 0; i < 20; i++) { size_t p = nx() % (len - (1 << 20)); text_insert(t, p, orig + p / 2, 1 << 20); text_delete(t, p, 1 << 20); } }
	else if (!strcmp(w, "bigdel")) { for (int i = 0; i < 5; i++) { size_t p = nx() % (len / 2); text_delete(t, p, len / 2); text_insert(t, p, orig + p, len / 2); } }
	else if (!strcmp(w, "screen")) { size_t L = text_lines(t); for (int i = 0; i < 100000; i++) { size_t s = text_line_start(t, nx() % (L + 1)); sink += text_read(t, s, buf, 4000); } }
	else if (!strcmp(w, "lines")) { size_t L = text_lines(t), s = 0; for (size_t l = 1; l <= L + 1; l++) { size_t e = text_line_start(t, l); sink += text_read(t, s, buf, e - s); s = e; } }
	else if (!strcmp(w, "readall")) { sink += text_read(t, 0, buf, len); }
	else if (!strcmp(w, "randedit")) { for (int i = 0; i < 50000; i++) { size_t l = text_len(t), p = nx() % (l + 1), k = 1 + nx() % 10; if (nx() & 1) text_insert(t, p, "abcdefghij", k); else text_delete(t, p, k > l - p ? l - p : k); } }
	else if (!strcmp(w, "lineof")) { for (int i = 0; i < 200000; i++) sink += text_line_of(t, nx() % (len + 1)); }
	else if (!strcmp(w, "linestart")) { size_t L = text_lines(t); for (int i = 0; i < 200000; i++) sink += text_line_start(t, nx() % (L + 1)); }
	else { fprintf(stderr, "?\n"); exit(2); }
}
int main(int argc, char **argv)
{
	if (argc != 3) return 2;
	FILE *f = fopen(argv[1], "rb");
	fseek(f, 0, SEEK_END); n = (size_t)ftell(f); rewind(f);
	orig = malloc(n); buf = malloc(n + 4096);
	if (fread(orig, 1, n, f) != n) return 2;
	fclose(f);
	size_t h0 = mallinfo2().uordblks + mallinfo2().hblkhd;
	Text *t = !strcmp(argv[2], "open") ? text_new("", 0) : text_new(orig, n);
	size_t h1 = mallinfo2().uordblks + mallinfo2().hblkhd;
	double a = now();
	run(&t, argv[2]);
	double b = now();
	size_t h2 = mallinfo2().uordblks + mallinfo2().hblkhd;
	printf("%s s=%.4f heap_open=%zu heap_after=%zu len=%zu sink=%zu\n", argv[2], b - a, h1 - h0, h2 - h0, text_len(t), sink % 1000);
	return 0;
}
