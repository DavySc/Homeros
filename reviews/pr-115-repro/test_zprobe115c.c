/* PR 115 re-review probe: Enter after moving the selection, and after the prompt narrows the list */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "check.h"
#include "core.h"
#include "keyname.h"
#include "snip.h"
#include "tools.h"
static char dir[] = "/tmp/homeros-p115c-XXXXXX";
static void first(const char *tag, Editor *e)
{
	char b[80]; size_t n = text_read(e->buf->text, 0, b, 79); b[n] = 0; b[strcspn(b, "\n")] = 0;
	printf("%s: [%s]\n", tag, b);
	while (e->mode != MODE_NORMAL || e->ks.visual) keys_feed(e, KEY_ESC, 0);
	keys_feed(e, 'u', 0);
}
int main(void)
{
	CHECK(mkdtemp(dir));
	char cfg[96], md[160], cmd[512], path[96];
	snprintf(cfg, sizeof cfg, "%s/cfg", dir); snprintf(md, sizeof md, "%s/homeros/snippets/sql.md", cfg);
	setenv("XDG_CONFIG_HOME", cfg, 1); file_mkdirs(md);
	snprintf(cmd, sizeof cmd, "cp %s %s", getenv("SNIPCAT"), md); CHECK(!system(cmd));
	snprintf(path, sizeof path, "%s/x_task_a.sql", dir);
	static Editor e; CHECK(!core_open(&e, path));
	const char *d, *b;
	for (int i = 0; i < 4; i++) { const char *n = snip_get(&e, NULL, 0, i, &d, &b); printf("item %d: %s, body [%.*s]\n", i, n, (int)strcspn(b, "\n"), b); }
	CHECK(!tools_pick(&e, PICK_SNIPPETS));
	for (int i = 0; i < 3; i++) tools_key(&e, KEY_DOWN);
	tools_key(&e, KEY_ENTER); first("Down x3, Enter", &e);
	CHECK(!tools_pick(&e, PICK_SNIPPETS));
	for (int i = 0; i < 40; i++) tools_key(&e, KEY_DOWN);
	for (const char *k = "trgre"; *k; k++) tools_key(&e, *k);
	tools_key(&e, KEY_ENTER); first("Down x40, then 'trgre', Enter", &e);
	return 0;
}
