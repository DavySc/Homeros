/* libFuzzer: the infoview child's HTTP side (src/infoview/serve/serve.c,
   included whole, its main renamed) on any bytes from a connection: no
   crash, no read outside them, and nothing from outside ROOT is ever sent,
   whatever the path (a file beside ROOT holds a marker that must never
   appear in an answer). */
#include <stdint.h>
#define main serve_main
#include "../src/infoview/serve/serve.c"
#undef main

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t n)
{
	static int set;
	static char dir[64];
	if (!set) {
		char p[128];
		strcpy(dir, "/tmp/homeros-ivfuzz-XXXXXX");
		if (!mkdtemp(dir)) abort();
		snprintf(p, sizeof p, "%s/root", dir);
		mkdir(p, 0700);
		root = strdup(p);
		snprintf(p, sizeof p, "%s/root/index.html", dir);
		FILE *f = fopen(p, "w"); fputs("page", f); fclose(f);
		snprintf(p, sizeof p, "%s/secret", dir);
		f = fopen(p, "w"); fputs("MARKER-OUTSIDE", f); fclose(f);
		strcpy(token, "tok");
		for (int i = 0; i < MAXC; i++) c[i].fd = -1;
		set = 1;
	}
	Conn k = { .fd = -1 };
	if (str_add(&k.in, (const char *)data, n) || requests(&k)) { free(k.in.s); free(k.out.s); return 0; }
	if (k.out.s && memmem(k.out.s, k.out.n, "MARKER-OUTSIDE", 14)) abort();
	if (k.page) conn_close(&k);
	free(k.in.s); free(k.out.s);
	/* what the editor was told, and the pages made, start afresh */
	toed.n = 0;
	for (int i = 0; i < MAXPAGE; i++) if (pages[i]) { free(pages[i]->outbox.s); free(pages[i]); pages[i] = NULL; }
	return 0;
}
