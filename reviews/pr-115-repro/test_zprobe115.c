/* PR 115 review probes: real catalogue (argv/env SNIPCAT), surround edge cases,
   picker vs popup ordering.  Prints results; CHECKs nothing. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "check.h"
#include "core.h"
#include "keyname.h"
#include "snip.h"
#include "tools.h"
static char dir[] = "/tmp/homeros-p115-XXXXXX";
static void feed(Editor *e, int k) { if (e->ks.pending || !snip_key(e, k, 0)) keys_feed(e, k, 0); }
static void dump(const char *tag, Editor *e)
{
	char b[4096]; size_t n = text_read(e->buf->text, 0, b, sizeof b - 1); b[n] = 0;
	printf("%s: [", tag);
	for (char *p = b; *p; p++) printf(*p == '\n' ? "\\n" : *p == '\t' ? "\\t" : "%c", *p);
	printf("] mode=%d line=%zu col=%zu\n", e->mode, e->win->line, e->win->col);
}
static void surround(const char *tag, const char *t, const char *k, const char *s)
{
	static Editor e; char path[96];
	snprintf(path, sizeof path, "%s/i.sql", dir);
	CHECK(!core_open(&e, path)); CHECK(!core_edit(&e, 0, 0, t, strlen(t)));
	while (*k) feed(&e, test_key(&k));
	int r = snip_insert(&e, s);
	printf("r=%d ", r); dump(tag, &e);
	feed(&e, KEY_ESC); feed(&e, 'u'); dump("  after u", &e);
	while (e.mode != MODE_NORMAL || e.ks.visual) feed(&e, KEY_ESC);
	core_free(&e);
}
int main(void)
{
	CHECK(mkdtemp(dir));
	char cfg[96], md[160], cmd[1024];
	snprintf(cfg, sizeof cfg, "%s/cfg", dir);
	snprintf(md, sizeof md, "%s/homeros/snippets/sql.md", cfg);
	setenv("XDG_CONFIG_HOME", cfg, 1);
	file_mkdirs(md);
	snprintf(cmd, sizeof cmd, "cp %s %s && cat >> %s <<'X'\n## 99. Probe\n```sql\n-- wrp : w\nbegin\n\t$0\nend\n```\n```sql\n-- wrp2 : two $0\n($0) ($0)\n```\n```sql\n-- wrp3 : no 0\na ${1:x} b\n```\nX\n", getenv("SNIPCAT"), md, md);
	CHECK(!system(cmd));
	{ static Editor e0; const char *d, *b; CHECK(!core_open(&e0, NULL)); snip_get(&e0, NULL, 0, 0, &d, &b); core_free(&e0); }
	/* surround edge cases */
	surround("CTRL-V narrow block", "abc\ndef", "<C-v>j", "wrp");
	surround("V from a blank-only line", "    \n    b", "Vj", "wrp");
	surround("V tab indent", "\ta\n\tb", "Vj", "wrp");
	surround("v$ over EOL", "abc\ndef", "v$", "wrp");
	surround("v to next line start", "abc\ndef", "vj0", "wrp");
	surround("CTRL-V block", "ab\ncd", "<C-v>jl", "wrp");
	surround("V two lines, indented", "    a\n      b\nc", "Vj", "wrp");
	surround("v in indent", "  ab", "0vl", "wrp");
	surround("$0 twice", "x", "v", "wrp2");
	surround("no $0, stop 1", "x", "v", "wrp3");
	surround("V empty line", "a\n\nb", "jV", "wrp");
	surround("v multibyte end", "aé", "v$", "wrp");
	/* picker vs popup per file group, real catalogue */
	const char *files[] = { "x_default_a.sql", "x_layout_a.sql", "x_proc_a.sql", "x_task_a.sql", "x_process_a.sql", "x_trigger_a.sql", "z.sql" };
	const char *qs[] = { "s", "set", "if", "ins", "upd", "ex", "msg", "try", "cnt", "hide" };
	int diff = 0, tot = 0;
	for (size_t f = 0; f < sizeof files / sizeof *files; f++) {
		static Editor e; char path[96];
		snprintf(path, sizeof path, "%s/%s", dir, files[f]);
		CHECK(!core_open(&e, path));
		const char *d, *b, *n0 = snip_get(&e, NULL, 0, 0, &d, &b);
		int i = 0; while (snip_get(&e, NULL, 0, i, &d, &b)) i++;
		printf("%s: %d snippets, first %s\n", files[f], i, n0);
		for (size_t q = 0; q < sizeof qs / sizeof *qs; q++) {
			keys_feed(&e, 'S', 0);
			for (const char *s = qs[q]; *s; s++) feed(&e, *s);
			char pop[64] = "-"; if (e.npum) snprintf(pop, sizeof pop, "%s", e.pum[0].word);
			while (e.mode != MODE_NORMAL) feed(&e, KEY_ESC);
			feed(&e, 'u');
			CHECK(!tools_pick(&e, PICK_SNIPPETS));
			for (const char *s = qs[q]; *s; s++) tools_key(&e, *s);
			/* Enter inserts the selected one; read its name from the buffer start is hard, so
			   compare by inserting via picker into empty buffer and checking body prefix */
			tools_key(&e, KEY_ENTER);
			while (e.mode != MODE_NORMAL || e.ks.visual) feed(&e, KEY_ESC);
			char got[80]; size_t gn = text_read(e.buf->text, 0, got, 79); got[gn] = 0; got[strcspn(got, "\n")] = 0;
			feed(&e, 'u');
			char want[80] = "-";
			if (e.npum || 1) { if (*pop != '-' && !snip_insert(&e, pop)) { while (e.mode != MODE_NORMAL || e.ks.visual) feed(&e, KEY_ESC);
				size_t wn = text_read(e.buf->text, 0, want, 79); want[wn] = 0; want[strcspn(want, "\n")] = 0; feed(&e, 'u'); } }
			int same = !strcmp(got, want) || (*pop == '-' && !*got);
			tot++; diff += !same;
			if (!same) printf("  %s q=%s popup first=%s [%s] picker gave [%s]\n", files[f], qs[q], pop, want, got);
		}
		core_free(&e);
	}
	printf("picker vs popup first pick differ: %d of %d\n", diff, tot);
	return 0;
}
