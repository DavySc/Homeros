/* G1: the results grid.  usage: g1 MODE CONN FILE.sql [N]
   dump : the results buffer, each line with its faces under it, the status, and whether every | of a grid sits in one column
   time : ms from F5 to the run's end, longest loop turn, bytes of results
   draw : after the run, N redraws of the results window at its end: ms each (best) */
#include <time.h>
#include "h.h"
#include "grid.h"
#include "uni.h"
static long long now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1000000000LL + t.tv_nsec; }
static int done(Buf *b)
{
	char t[64];
	size_t z = text_len(b->text), a = z > 40 ? z - 40 : 0, n = text_read(b->text, a, t, z - a);
	t[n] = 0;
	return strstr(t, "Completion time") != NULL;
}
static char fl(int f)
{
	return f == FACE_NONE ? '.' : f == FACE_PMENU_SEL ? 'H' : f == FACE_ORG_TABLE ? '|' : f == FACE_LINE_NR ? '#' : f == FACE_DIAG_WARN ? 'N'
		: f == FACE_NUMBER ? '~' : f == FACE_FUNCTION ? 'M' : f == FACE_DIAG_ERROR ? 'E' : '?';
}
int main(int argc, char **argv)
{
	const char *m = argv[1];
	char path[4096], *sql = malloc(1 << 24);
	FILE *f = fopen(argv[3], "r"); size_t n = fread(sql, 1, (1 << 24) - 1, f); sql[n] = 0; fclose(f);
	setup();
	snprintf(path, sizeof path, "%s/q.sql", argv[2]);
	if (strcmp(argv[2], "ms") && strcmp(argv[2], "pg")) {   /* another connection line: name=rest */
		char conf[4096]; snprintf(conf, sizeof conf, "%s/homeros/sql", dir);
		FILE *c = fopen(conf, "a"); fprintf(c, "%s\n", argv[2]); fclose(c);
		snprintf(path, sizeof path, "q.sql");
	}
	put(path, sql, 0600);
	core_open(&e, path);
	if (strcmp(argv[2], "ms") && strcmp(argv[2], "pg")) { char nm[64]; sscanf(argv[2], "%63s", nm); sql_connect(&e, nm); }
	long long t0 = now(), worst = 0, t;
	if (sql_key(&e, KEY_F5) != 1 || e.msg[0]) { printf("F5: %s\n", e.msg); }
	Win *w = rwin();
	if (!w) { printf("no results window: %s\n", e.msg); return 1; }
	int wait = argc > 5 ? atoi(argv[5]) : 60000;
	if (!strcmp(m, "peek") || !strcmp(m, "cancel")) {
		for (int i = 0; i < 1000 && !done(w->buf); i++) {
			loop_once(10);
			if (i % 50 == 49) printf("at %.0f ms: %zu bytes in the results\n", (double)(now() - t0) / 1e6, text_len(w->buf->text));
			if (!strcmp(m, "cancel") && i == 150) { printf("Alt+Break -> %d\n", sql_key(&e, KEY_BREAK)); for (int j = 0; j < 100; j++) loop_once(10); break; }
		}
		m = "dump";
	}
	if (!strcmp(m, "toggle")) {
		for (int i = 0; i < 1000 && !done(w->buf); i++) loop_once(10);
		Buf *rb = w->buf; size_t z0 = text_len(rb->text);
		printf("close: %d, windows %d\n", sql_results(&e), nwins());
		printf("open: %d, windows %d\n", sql_results(&e), nwins());
		w = rwin();
		printf("same buffer %d, same text %d, faces %d, status %s, cursor in query %d\n", w && w->buf == rb, w && text_len(w->buf->text) == z0, w && w->buf->faces != NULL,
#ifdef HAS_GRID
			w ? w->buf->status : "-",
#else
			"-",
#endif
			e.win != w);
		core_win_goto(&e, 'j', 1); keys(":bd\r");
		printf("after :bd in the results: windows %d, msg [%s]\n", nwins(), e.msg);
		printf("F5 again: %d [%s]\n", sql_key(&e, KEY_F5), e.msg);
		w = rwin(); if (!w) { printf("no results window\n"); return 0; }
		m = "dump";
	}
	for (int i = 0; i < wait / 5 && !done(w->buf); i++) { t = now(); loop_once(5); t = now() - t; if (t > worst && t < 5000000) worst = t; if (t >= 5000000 && t - 5000000 > worst) worst = t - 5000000; }
	t = now() - t0;
	Buf *b = w->buf;
	if (!strcmp(m, "time")) { printf("%.1f ms, longest turn over its wait %.1f ms, %zu bytes, %zu lines\n", (double)t / 1e6, (double)worst / 1e6, text_len(b->text), (size_t)text_lines(b->text)); return 0; }
	if (!strcmp(m, "draw")) {
		Grid g = {0}; grid_resize(&g, 50, 200);
		e.rows = 50; e.cols = 200;
		int N = argc > 4 ? atoi(argv[4]) : 100;
		core_win_goto(&e, 'j', 1);
		keys("G");
		long long best = 1LL << 60, tot = 0;
		for (int i = 0; i < N; i++) { keys(i % 2 ? "k" : "j"); t = now(); core_draw(&e, e.tab, &g); t = now() - t; tot += t; if (t < best) best = t; }
		printf("draw at the end: best %.3f ms, mean %.3f ms of %d\n", (double)best / 1e6, (double)tot / 1e6 / N, N);
		keys("gg");
		best = 1LL << 60, tot = 0;
		for (int i = 0; i < N; i++) { keys(i % 2 ? "k" : "j"); t = now(); core_draw(&e, e.tab, &g); t = now() - t; tot += t; if (t < best) best = t; }
		printf("draw at the top: best %.3f ms, mean %.3f ms of %d\n", (double)best / 1e6, (double)tot / 1e6 / N, N);
		return 0;
	}
	size_t z = text_len(b->text), nl = text_lines(b->text);
	char *s = malloc(z + 1); unsigned char *fc = malloc(z + 1);
	text_read(b->text, 0, s, z); s[z] = 0;
	int bad8 = 0; for (size_t i = 0; i < z; i++) if ((unsigned char)s[i] >= 0xF8) bad8++;
#ifdef HAS_GRID
	printf("status: %s\n", b->status ? b->status : "(null)");
#endif
	int prev[256], np = -1, mis = 0;
	for (size_t l = 0, a = 0; a < z; l++) {
		char *e2 = memchr(s + a, '\n', z - a); size_t k = e2 ? (size_t)(e2 - s) - a : z - a;
		printf("%.*s\n", (int)k, s + a);
#ifdef HAS_GRID
		if (b->faces && k) {
			/* faces asked in 256-byte windows, as draw_line does */
			for (size_t o = 0; o < k; o += 256) b->faces(b, a + o, a + (o + 256 < k ? o + 256 : k), fc + o);
			/* one letter per cell */
			for (size_t i = 0; i < k; ) { size_t gl = uni_glen((unsigned char *)s + a, k, i); int cw = uni_width((unsigned char *)s + a, k, i); for (int c = 0; c < cw; c++) putchar(fl(fc[i])); i += gl; }
			putchar('\n');
		}
#endif
		int cols[256], nc = 0;
		for (size_t i = 0; i + 2 < k; i++) if (!memcmp(s + a + i, "\xe2\x94\x82", 3) || !memcmp(s + a + i, "\xe2\x94\xbc", 3)) { if (nc < 256) cols[nc++] = grid_width(s + a, i); }
		if (nc && np > 0 && (nc != np || memcmp(cols, prev, (size_t)nc * sizeof *cols))) { mis++; printf("^^^ MISALIGNED line %zu\n", l + 1); }
		np = nc ? nc : -1; memcpy(prev, cols, sizeof cols);
		a += k + 1;
	}
	printf("== %zu lines, %d misaligned, %d bytes >= 0xF8 in the buffer\n", nl, mis, bad8);
	return 0;
}
