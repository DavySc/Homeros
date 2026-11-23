/* PR 115 probe: the file's last line break after surround of v$ on the last line */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "check.h"
#include "core.h"
#include "keyname.h"
#include "snip.h"
static char dir[] = "/tmp/homeros-p115b-XXXXXX";
static void feed(Editor *e, int k) { if (e->ks.pending || !snip_key(e, k, 0)) keys_feed(e, k, 0); }
static void keys(Editor *e, const char *k) { while (*k) feed(e, test_key(&k)); }
static void show(const char *tag, Editor *e, const char *path)
{
	char b[256]; size_t n = text_read(e->buf->text, 0, b, sizeof b - 1); b[n] = 0;
	printf("%s: lines=%zu text=[", tag, core_lines(e));
	for (char *p = b; *p; p++) printf(*p == '\n' ? "\\n" : "%c", *p);
	FILE *f = fopen(path, "r"); n = f ? fread(b, 1, sizeof b - 1, f) : 0; if (f) fclose(f); b[n] = 0;
	printf("] file=[");
	for (char *p = b; *p; p++) printf(*p == '\n' ? "\\n" : "%c", *p);
	printf("]\n");
}
int main(void)
{
	CHECK(mkdtemp(dir));
	char cfg[96], md[160], path[96];
	snprintf(cfg, sizeof cfg, "%s/cfg", dir);
	snprintf(md, sizeof md, "%s/homeros/snippets/sql.md", cfg);
	setenv("XDG_CONFIG_HOME", cfg, 1);
	file_mkdirs(md);
	FILE *c = fopen(md, "w"); fputs("## 1. X\n```sql\n-- wrp : w\nbegin\n\t$0\nend\n```\n", c); fclose(c);
	snprintf(path, sizeof path, "%s/a.sql", dir);
	static Editor e;
	const char *d, *b;
	CHECK(!core_open(&e, path)); CHECK(!core_edit(&e, 0, 0, "ab", 2));
	keys(&e, "v$cX<Esc>:w<CR>"); show("v$cX", &e, path); core_free(&e);
	CHECK(!core_open(&e, path)); CHECK(!core_edit(&e, 0, text_len(e.buf->text), "ab\n", 3));
	snip_get(&e, NULL, 0, 0, &d, &b);
	keys(&e, "v$"); CHECK(!snip_insert(&e, "wrp"));
	keys(&e, "<Esc>"); show("surround v$ (before :w)", &e, path);
	keys(&e, ":w<CR>"); show("surround v$ :w", &e, path);
	keys(&e, "Go<Esc>"); show("then Go", &e, path);
	core_free(&e);
	return 0;
}
