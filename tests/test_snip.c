/* Snippets (package 19).  Replays the cases src/snip/conform/snip.py
   recorded from Neovim 0.12's vim.snippet (src/snip/conform/recorded.txt):
   a body expanded after a line's text, then keys; the lines, the cursor
   and the mode must match.  With a FILE of cases it prints its own results
   instead, for snip.py to compare with a live Neovim.  Each body is reached
   as Davy reaches it: a catalogue in the config folder, its name typed in
   a .sql buffer, CTRL-Y in the popup. */
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "check.h"
#include "core.h"
#include "keyname.h"
#include "snip.h"
#include "tools.h"

enum { MAXC = 4096 };
static char *body[MAXC], *pre[MAXC], *keys[MAXC], *want[MAXC];
static int n;
static char dir[] = "/tmp/homeros-snip-XXXXXX";

/* a line with \n, \t and \\ escapes, in place */
static char *unescape(char *s)
{
	char *w = s;
	for (char *r = s; *r; r++) *w++ = *r != '\\' ? *r : *++r == 'n' ? '\n' : *r == 't' ? '\t' : *r;
	*w = 0;
	return s;
}

static char *line(FILE *f)
{
	static char b[1 << 16];
	if (!fgets(b, sizeof b, f)) return NULL;
	b[strcspn(b, "\n")] = 0;
	return strdup(b);
}

static void feed(Editor *e, int k)
{
	if (e->ks.pending || !snip_key(e, k, 0)) keys_feed(e, k, 0);   /* as main.c */
}


/* one case: its result as snip.py prints Neovim's */
static void run(int i, char *out, size_t cap)
{
	static Editor e;
	char path[64], name[16];
	snprintf(path, sizeof path, "%s/c%d.sql", dir, i);
	CHECK(!core_open(&e, path));
	CHECK(!core_edit(&e, 0, 0, pre[i], strlen(pre[i])));
	keys_feed(&e, 'A', 0);
	snprintf(name, sizeof name, "s%04d", i);
	for (const char *s = name; *s; s++) feed(&e, *s);
	feed(&e, 25);
	for (const char *s = keys[i]; *s; ) {
		if (*s == ' ') { s++; continue; }
		int k = test_key(&s);
		if (k != 23 || e.mode != MODE_NORMAL || e.ks.visual || e.ks.pending) feed(&e, k);   /* as snip.py */
	}
	size_t w = (size_t)snprintf(out, cap, "%c %zu %zu ", e.ks.visual ? 'v' : e.mode == MODE_INSERT ? 'i' : 'n', e.win->line + 1, e.win->col);
	for (size_t l = 0; l < core_lines(&e); l++) {
		size_t len;
		const unsigned char *s = core_line(&e, l, &len);
		for (size_t j = 0; j < len && w + 3 < cap; j++)
			w += (size_t)snprintf(out + w, cap - w, s[j] == '\\' ? "\\\\" : s[j] == '\t' ? "\\t" : "%c", s[j]);
		if (l + 1 < core_lines(&e) && w + 3 < cap) w += (size_t)snprintf(out + w, cap - w, "\\n");
	}
	/* leave the snippet, so the next case starts without one */
	while (e.mode != MODE_NORMAL || e.ks.visual) feed(&e, KEY_ESC);
	feed(&e, KEY_ESC);
	core_free(&e);
}

/* text typed in Insert mode at the end of a new buffer called name (NULL:
   no name) */
static Editor *typed(const char *name, const char *s)
{
	static Editor e;
	char path[64];
	snprintf(path, sizeof path, "%s/%s", dir, name);
	CHECK(!core_open(&e, name ? path : NULL));
	keys_feed(&e, 'A', 0);
	for (; *s; s++) feed(&e, *s);
	return &e;
}

static void is(Editor *e, const char *want)
{
	char b[64];
	size_t n = text_read(e->buf->text, 0, b, sizeof b - 1);
	b[n] = 0;
	CHECK(!strcmp(b, want));
	while (e->mode != MODE_NORMAL || e->ks.visual) feed(e, KEY_ESC);
	core_free(e);
}

/* The catalogue's odd blocks and the popup's keys (Davy's nvim-cmp) */
static void popup(void)
{
	Editor *e = typed("p.sql", "pure");
	feed(e, 25);
	is(e, "SELECT 1\n");
	e = typed("p.sql", "notsql");
	CHECK(!e->npum);
	is(e, "notsql\n");
	e = typed("p.txt", "pure");   /* only in .sql buffers */
	CHECK(!e->pum);
	feed(e, 25);
	is(e, "pure\n");   /* CTRL-Y: the character above, none */
	e = typed(NULL, "pure");
	CHECK(!e->pum);
	is(e, "pure\n");
	/* CTRL-N, Tab and Down pick the next, CTRL-P, S-Tab and Up the one
	   before, past either end to none; CTRL-E closes; Enter with none
	   picked is a line break */
	e = typed("p.sql", "s00");
	int n = e->npum;
	CHECK(n > 2 && e->pumsel == -1);
	int k[] = { 14, KEY_TAB, KEY_DOWN, KEY_S_TAB, 16, KEY_UP, 16, 14 }, want[] = { 0, 1, 2, 1, 0, -1, n - 1, -1 };
	for (int i = 0; i < 8; i++) {
		feed(e, k[i]);
		CHECK(e->pumsel == want[i] && e->npum == n);
	}
	feed(e, 5);
	CHECK(!e->pum);
	feed(e, '0');
	CHECK(e->pum && e->pumsel == -1);
	feed(e, KEY_ENTER);
	CHECK(!e->pum);
	is(e, "s000\n\n");
	/* Enter takes a picked item */
	e = typed("p.sql", "pur");
	feed(e, 14);
	feed(e, KEY_ENTER);
	is(e, "SELECT 1\n");
	puts("test_snip: the catalogue's odd blocks and the popup's keys");
}

/* The group from the file name (Davy's <prefix>_<group>_<name>.sql) comes
   first; task snippets show in a task file only */
static void groups(void)
{
	static const struct { const char *file, *first; int n; } c[] = {
		{ "x_default_a_default_b.sql", "gdef", 2 }, { "x_y_task_z.sql", "gtsk", 3 },
		{ "x_proc_default.sql", "ghlp", 2 }, { "x_process_z.sql", "gdef", 2 }, { "tasks.sql", "gdef", 2 },
	};
	for (size_t i = 0; i < sizeof c / sizeof *c; i++) {
		Editor *e = typed(c[i].file, "g");
		CHECK(e->npum == c[i].n && !strcmp(e->pum[0].word, c[i].first));
		for (int j = 0; j < e->npum; j++) CHECK(strcmp(e->pum[j].word, "gtsk") || c[i].n == 3);
		feed(e, KEY_ESC);
		core_free(e);
	}
	puts("test_snip: the file's group first, task snippets in task files only");
}

/* text t in a new buffer called name, then keys k in Normal mode, then
   snippet s inserted as SPC i s does */
static Editor *inserted(const char *name, const char *t, const char *k, const char *s)
{
	static Editor e;
	char path[64];
	snprintf(path, sizeof path, "%s/%s", dir, name);
	CHECK(!core_open(&e, path));
	CHECK(!core_edit(&e, 0, 0, t, strlen(t)));
	while (*k) feed(&e, test_key(&k));
	CHECK(!snip_insert(&e, s));
	return &e;
}

/* SPC i s: the list in the popup's order, a snippet at the cursor, and
   around the Visual area at its $0 (surround with) */
static void insert(int total)
{
	Editor *e = typed("x_proc_a.sql", "");
	const char *d, *b, *nm = snip_get(e, 0, &d, &b);
	CHECK(nm && !strcmp(nm, "ghlp") && !strcmp(d, "h") && !strcmp(b, "H"));
	int i = 1;
	while (snip_get(e, i, &d, &b)) i++;
	CHECK(i == total);
	CHECK(snip_insert(e, "nosuch") == -1);
	is(e, "\n");
	e = inserted("i.sql", "q", "", "pure");
	CHECK(e->mode == MODE_INSERT && e->win->col == 8);
	is(e, "SELECT 1q\n");
	e = inserted("i.sql", "x = 1", "$v", "wrp");
	is(e, "x = begin\n    1\nend\n");
	e = inserted("i.sql", "x", "v", "pure");   /* no $0: the area goes at the end */
	is(e, "SELECT 1x\n");
	e = inserted("i.sql", "    a\n    b\nc", "Vj", "wrp");
	is(e, "    begin\n        a\n        b\n    end\nc\n");
	/* through the picker: the name typed, its description and body shown,
	   Enter puts it around the Visual area */
	static Editor pe;
	char path[64];
	snprintf(path, sizeof path, "%s/pick.sql", dir);
	CHECK(!core_open(&pe, path) && !core_edit(&pe, 0, 0, "1", 1));
	feed(&pe, 'v');
	CHECK(!tools_pick(&pe, PICK_SNIPPETS));
	for (const char *k = "wrp"; *k; k++) CHECK(tools_key(&pe, *k));
	static Grid g;
	CHECK(!grid_resize(&g, 30, 100));
	tools_draw(&pe, &g);
	static char screen[30 * 401];
	screen[0] = 0;
	for (int r = 0; r < 30; r++, strcat(screen, "\n"))
		for (int c = 0; c < 100; c++) strcat(screen, (const char *)grid_text(&g.c[r * 100 + c]));
	CHECK(strstr(screen, "wrp w") && strstr(screen, "begin"));
	grid_free(&g);
	CHECK(tools_key(&pe, KEY_ENTER));
	is(&pe, "begin\n    1\nend\n");
	puts("test_snip: SPC i s lists, inserts and surrounds");
}

int main(int argc, char **argv)
{
	FILE *f = fopen(argc > 1 ? argv[1] : "src/snip/conform/recorded.txt", "r");
	CHECK(f);
	for (char *b; n < MAXC && (b = line(f)); n++) {
		body[n] = unescape(b);
		CHECK((pre[n] = line(f)) && (keys[n] = line(f)));
		unescape(pre[n]);
		if (argc == 1) CHECK((want[n] = line(f)));
	}
	fclose(f);
	CHECK(mkdtemp(dir));
	char cfg[64], md[96];
	snprintf(cfg, sizeof cfg, "%s/cfg", dir);
	snprintf(md, sizeof md, "%s/homeros/snippets/sql.md", cfg);
	setenv("XDG_CONFIG_HOME", cfg, 1);
	file_mkdirs(md);
	FILE *c = fopen(md, "w");
	CHECK(c);
	/* written as Windows saves it (CRLF), so the bodies' line breaks pass
	   file_eol(); then a block with no description, one with no name line
	   and one in another language, which are no snippets (no name holds a,
	   x or é, which the cases type: no popup opens there) */
	for (int i = 0; i <= n; i++) {
		static char b[1 << 17];
		if (i < n) snprintf(b, sizeof b, "```sql\n-- s%04d : case %d\n%s\n```\n", i, i, body[i]);
		else strcpy(b, "```sql\n-- pure\nSELECT 1\n```\n```sql\nno name\n```\n```text\n-- notsql : x\nY\n```\n"
		               "## 6. Task (default)\n```sql\n-- gtsk : t\nT\n```\n## 2. Default\n```sql\n-- gdef : d\nD\n```\n"
		               "## 11. Shared helpers\n```sql\n-- ghlp : h\nH\n```\n```sql\n-- wrp : w\nbegin\n\t$0\nend\n```\n");
		for (char *p = b; *p; p++) fputs(*p == '\n' ? "\r\n" : (char[]){ *p, 0 }, c);
	}
	CHECK(!fclose(c));
	int bad = 0;
	for (int i = 0; i < n; i++) {
		static char got[1 << 16];
		run(i, got, sizeof got);
		if (argc > 1) puts(got);
		else if (strcmp(got, want[i])) {
			if (bad++ < 5) fprintf(stderr, "case %d: keys %s\n  nvim %s\n  ours %s\n", i, keys[i], want[i], got);
		}
	}
	if (argc == 1) {
		printf("test_snip: %d of %d recorded vim.snippet cases differ\n", bad, n);
		CHECK(!bad);
		popup();
		groups();
		insert(n + 4);
	}
	return 0;
}
