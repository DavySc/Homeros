/* Review of PR 105: :Format and gq through the real verible-verilog-format
   on every file given; the buffer must equal what verible prints for the
   same text, u must give the text back, a second :Format changes nothing.
   usage: test_keys_zzsv FILE... */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "check.h"
#include "core.h"
#include "keys.h"

static void keys(Editor *e, const char *s)
{
	for (; *s; s++) keys_feed(e, *s == '\n' ? KEY_ENTER : *s == 27 ? KEY_ESC : (unsigned char)*s, s[1] != 0);
}
static char *text(Editor *e, size_t *n)
{
	size_t len = text_len(e->buf->text);
	char *b = malloc(len + 1);
	*n = text_read(e->buf->text, 0, b, len);
	b[*n] = 0;
	return b;
}
static char *run(const char *cmd, size_t *n)
{
	FILE *p = popen(cmd, "r");
	size_t cap = 1 << 22;
	char *b = malloc(cap);
	*n = fread(b, 1, cap - 1, p);
	b[*n] = 0;
	int st = pclose(p);
	if (st) { free(b); return NULL; }
	return b;
}
static long long ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1000LL + t.tv_nsec / 1000000; }

int main(int argc, char **argv)
{
	int differ = 0, failed = 0, gqdiff = 0;
	long long worst = 0;
	srand(1);
	for (int i = 1; i < argc; i++) {
		Editor e;
		char cmd[4096];
		size_t n0, n1, n2, nw;
		if (core_open(&e, argv[i])) { printf("%s: cannot open\n", argv[i]); continue; }
		char *orig = text(&e, &n0);
		snprintf(cmd, sizeof cmd, "verible-verilog-format --indentation_spaces=4 - < %s 2>/dev/null", argv[i]);
		char *want = run(cmd, &nw);
		long long t0 = ms();
		e.msg[0] = 0;
		keys(&e, ":Format\n");
		long long dt = ms() - t0;
		if (dt > worst) worst = dt;
		char *got = text(&e, &n1);
		if (!want) { failed++; printf("%s: verible fails; msg '%s'; text %s\n", argv[i], e.msg, n0 == n1 && !memcmp(orig, got, n0) ? "unchanged" : "CHANGED"); }
		else if (nw != n1 || memcmp(want, got, nw)) { differ++; printf("%s: :Format differs from verible (%zu vs %zu bytes) msg '%s' %lld ms\n", argv[i], n1, nw, e.msg, dt); }
		if (want) {
			keys(&e, ":Format\n");
			char *again = text(&e, &n2);
			if (n2 != n1 || memcmp(again, got, n1)) printf("%s: second :Format changes the text\n", argv[i]);
			free(again);
			if (n1 != n0 || memcmp(got, orig, n0)) {
				keys(&e, "u");
				char *back = text(&e, &n2);
				if (n2 != n0 || memcmp(back, orig, n0)) printf("%s: u does not give the text back\n", argv[i]);
				free(back);
			}
		}
		free(got);
		core_free(&e);
		/* gq over a random line range against verible --lines */
		if (want && !core_open(&e, argv[i])) {
			size_t lines = text_lines(e.buf->text);
			for (int k = 0; k < 3 && lines > 2; k++) {
				size_t a = 1 + (size_t)rand() % (lines - 1), b = a + (size_t)rand() % 30;
				if (b > lines - 1) b = lines - 1;
				char kk[64];
				snprintf(kk, sizeof kk, "%zuGV%zuGgq", a, b);
				keys(&e, kk);
				got = text(&e, &n1);
				snprintf(cmd, sizeof cmd, "verible-verilog-format --indentation_spaces=4 --lines=%zu-%zu - < %s 2>/dev/null", a, b, argv[i]);
				char *w2 = run(cmd, &nw);
				if (w2 && (nw != n1 || memcmp(w2, got, nw))) { gqdiff++; printf("%s: gq %zu-%zu differs from verible --lines, msg '%s'\n", argv[i], a, b, e.msg); }
				if (!w2) printf("%s: verible --lines=%zu-%zu fails\n", argv[i], a, b);
				free(w2); free(got);
				keys(&e, "u");
				got = text(&e, &n1);
				if (n1 != n0 || memcmp(got, orig, n0)) { printf("%s: u after gq does not give the text back\n", argv[i]); free(got); break; }
				free(got);
			}
			core_free(&e);
		}
		free(orig); free(want);
	}
	printf("%d files: %d differ from verible, %d verible failures, %d gq ranges differ; slowest :Format %lld ms\n", argc - 1, differ, failed, gqdiff, worst);
	return 0;
}
