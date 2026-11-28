/* How long one loop turn is held when a semantic tokens answer arrives:
   hold LINES WORDS [ERRFROM].  Prints the longest loop_once(0) while the
   answer came, and the time of 1,000 lsp_faces calls of 256 bytes. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "core.h"
#include "loop.h"
#include "lsp.h"
#include "syntax.h"
#include "text.h"
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }
static int shown(Editor *e) { unsigned char f[4]; lsp_faces(e->buf, 0, 4, f); return f[0] != 0; }
int main(int argc, char **argv)
{
	int lines = atoi(argv[1]), words = atoi(argv[2]);
	static const char *sargv[] = { "python3", NULL, NULL, NULL, NULL, NULL };
	static char *const roots[] = { NULL };
	static LspServer table[2];
	char fake[1024];
	snprintf(fake, sizeof fake, "%s/fake.py", getenv("REPRO"));
	sargv[1] = fake; sargv[2] = argv[1]; sargv[3] = argv[2]; sargv[4] = argc > 3 ? argv[3] : NULL;
	table[0] = (LspServer){ ".fake", "fake", sargv, (const char *const *)roots, 0 };
	lsp_servers = table;
	char path[256];
	snprintf(path, sizeof path, "/tmp/hold-%d.fake", (int)getpid());
	FILE *f = fopen(path, "w");
	for (int l = 0; l < lines; l++) { for (int w = 0; w < words; w++) fputs("x ", f); fputc('\n', f); }
	fclose(f);
	Editor e;
	if (core_open(&e, path) || !e.buf->lsp) return 1;
	for (int round = 0; round < 6; round++) {
		double worst = 0, t0 = now(), sum = 0;
		int was = shown(&e);
		if (round) { core_edit(&e, 0, 0, "x ", 2); core_edit(&e, 0, 0, "", 2); lsp_flush(e.buf->lsp); }
		/* until the answer of this round is in: faces of byte 0 go away with an edit? no: wait a fixed time */
		while (now() - t0 < (round ? 1500 : 4000)) {
			double a = now();
			loop_once(0);
			double d = now() - a;
			if (d > worst) worst = d;
			sum += d;
			usleep(200);
		}
		printf("round %d: longest loop turn %.2f ms, tokens shown before %d after %d\n", round, worst, was, shown(&e));
	}
	unsigned char fc[256];
	size_t n = text_len(e.buf->text);
	double a = now();
	for (int i = 0; i < 100000; i++) lsp_faces(e.buf, (size_t)i * 7919 % (n - 256), (size_t)i * 7919 % (n - 256) + 256, fc);
	printf("lsp_faces: %.3f us per 256 bytes\n", (now() - a) * 10 / 1000);
	a = now();
	for (int i = 0; i < 200; i++) { core_edit(&e, 0, 0, "y", 1); }
	printf("edit with %d tokens: %.1f us per key\n", lines * words, (now() - a) * 1000 / 200);
	core_free(&e);
	unlink(path);
	return 0;
}
