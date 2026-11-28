/* Snippets (package 19).  Replays the cases src/snip/conform/snip.py
   recorded from Neovim 0.12's vim.snippet (src/snip/conform/recorded.txt):
   a body expanded after a line's text, then keys; the lines, the cursor
   and the mode must match.  With a FILE of cases it prints its own results
   instead, for snip.py to compare with a live Neovim.  Each body comes from
   a catalogue in the config folder, inserted as SPC i s does, in a buffer
   with no popup, as Neovim's. */
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "check.h"
#include "core.h"
#include "keyname.h"
#include "loop.h"
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
	snprintf(path, sizeof path, "%s/c%d.txt", dir, i);   /* no popup: Neovim's has none */
	CHECK(!core_open(&e, path));
	CHECK(!core_edit(&e, 0, 0, pre[i], strlen(pre[i])));
	keys_feed(&e, 'A', 0);
	snprintf(name, sizeof name, "s%04d", i);
	CHECK(!snip_insert(&e, name));
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
	e = typed("p.txt", "pure ");   /* only in .sql buffers */
	CHECK(!e->pum);
	CHECK(!snip_insert(e, "two"));   /* a snippet there too */
	feed(e, 's');
	CHECK(!e->pum);
	feed(e, 25);
	while (e->mode != MODE_INSERT) feed(e, KEY_ESC), feed(e, 'A');
	feed(e, 25);
	is(e, "pure s = b\n");   /* CTRL-Y: the character above, none */
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
		int j = 0, n = 0;
		while (j < e->npum && strcmp(e->pum[j].kind, "Snippet")) j++;   /* T-SQL's words come first */
		CHECK(j < e->npum && !strcmp(e->pum[j].word, c[i].first));
		for (; j < e->npum; j++, n++) CHECK(strcmp(e->pum[j].word, "gtsk") || c[i].n == 3);
		CHECK(n == c[i].n);
		feed(e, KEY_ESC);
		core_free(e);
	}
	/* only a heading's words before its '(' name its group */
	Editor *e = typed("p.sql", "dtz");
	CHECK(e->npum == 1);
	is(e, "dtz\n");
	puts("test_snip: the file's group first, task snippets in task files only");
}

/* text t with the cursor at its '|' (never a line's first byte) in a new
   .sql buffer, Insert mode there, then the keys of k typed */
static Editor *at(const char *t, const char *k)
{
	static Editor e;
	char path[64];
	const char *bar = strchr(t, '|');
	snprintf(path, sizeof path, "%s/c.sql", dir);
	CHECK(!core_open(&e, path));
	CHECK(!core_edit(&e, 0, 0, t, (size_t)(bar - t)) && !core_edit(&e, (size_t)(bar - t), 0, bar + 1, strlen(bar + 1)));
	e.win->line = text_line_of(e.buf->text, (size_t)(bar - t));
	e.win->col = (size_t)(bar - t) - core_line_start(&e, e.win->line) - 1;
	keys_feed(&e, 'a', 0);   /* 'i' at a line's end would go before its last byte */
	for (; *k; k++) feed(&e, *k);
	return &e;
}

/* typing pauses: the event loop runs 1.2 s, past snip.c's HOLD */
static void typing_pause(void)
{
	long long t0 = loop_now();
	while (loop_now() - t0 < 1200000000LL) loop_once(100);
}

/* Before any popup the index is built on the pause and the timer stays
   off: an idle editor is not woken (DESIGN.md: 0 idle wake-ups) */
static void quiet(void)
{
	Editor *e = at("select o.x from dbo.orders o|", " 1");
	CHECK(!e->pum);
	long long t0 = loop_now();
	loop_once(900);   /* a buffer without an index gets it at once, not after a pause */
	CHECK(loop_now() - t0 < 500000000LL);
	typing_pause();
	t0 = loop_now();
	loop_once(1500);
	CHECK(loop_now() - t0 > 1400000000LL);
	feed(e, KEY_ESC);
	core_free(e);
}

/* the popup's words, space-separated, in its order */
static const char *shown(Editor *e)
{
	static char b[4096];
	b[0] = 0;
	for (int i = 0; i < e->npum && strlen(b) + strlen(e->pum[i].word) + 2 < sizeof b; i++)
		strcat(strcat(b, i ? " " : ""), e->pum[i].word);
	return b;
}

/* What the popup offers by where the cursor is in its statement (the
   formatter's statement tree), as SSMS's IntelliSense does, matched by
   word parts as nvim-cmp does */
static void complete(void)
{
	static const char *const file =
		"create table #t (id int, name varchar(10), primary key (id))\n"
		"insert into log (at, msg) values (1, 'a')\n"
		"select c.city from dbo.customers c join orders as o on o.cust = c.id\n"
		"declare @count int, @w table (a int)\n";
	char t[1024];
	/* after Q. the columns of Q's table, wherever the alias is given */
	snprintf(t, sizeof t, "%sselect c|, o.cust from customers c join orders o on 1 = 1", file);
	Editor *e = at(t, ".");
	CHECK(!strcmp(shown(e), "id city"));
	feed(e, KEY_ESC), core_free(e);
	snprintf(t, sizeof t, "%sselect #t|", file);
	e = at(t, ".");
	CHECK(!strcmp(shown(e), "id name"));
	feed(e, KEY_ESC), core_free(e);
	snprintf(t, sizeof t, "%sselect l| from log l", file);
	e = at(t, ".m");
	CHECK(!strcmp(shown(e), "msg"));
	feed(e, KEY_ESC), core_free(e);
	/* in an IF's condition (a leaf of the statement tree too) */
	snprintf(t, sizeof t, "%sif exists (select 1 from log l where l| = 1) print 1", file);
	e = at(t, ".");
	CHECK(!strcmp(shown(e), "at msg"));
	feed(e, KEY_ESC), core_free(e);
	/* an alias of no table: nothing */
	snprintf(t, sizeof t, "%sselect x|", file);
	e = at(t, ".");
	CHECK(!e->pum);
	feed(e, KEY_ESC), core_free(e);
	/* after FROM the file's tables only, a CTE too */
	snprintf(t, sizeof t, "%swith recent as (select 1 x) select * from |", file);
	e = at(t, "r");
	CHECK(!strcmp(shown(e), "recent"));
	feed(e, KEY_ESC), core_free(e);
	snprintf(t, sizeof t, "%sselect * from |", file);
	e = at(t, "o");
	CHECK(!strcmp(shown(e), "orders"));
	feed(e, 25);
	size_t len;
	const unsigned char *l = core_line(e, e->win->line, &len);
	CHECK(len == 20 && !memcmp(l, "select * from orders", 20) && e->win->col == 20 && !e->pum);
	feed(e, KEY_ESC), core_free(e);
	/* @: the variables */
	snprintf(t, sizeof t, "%sset |", file);
	e = at(t, "@c");
	CHECK(!strcmp(e->pum[0].word, "@count") && !strcmp(e->pum[0].kind, "Variable") && !strstr(shown(e), " c"));
	feed(e, KEY_ESC), core_free(e);
	/* elsewhere: T-SQL's words, the exact one first, and the statement's
	   tables, aliases and their columns; snippets after them */
	snprintf(t, sizeof t, "%sselect | from orders o", file);
	e = at(t, "c");
	CHECK(strstr(shown(e), "cust") && !strstr(shown(e), "city"));
	feed(e, 'a'), feed(e, 's');
	CHECK(!strcmp(shown(e), "case cast cascade"));
	feed(e, KEY_ESC), core_free(e);
	e = typed("p.sql", "a");   /* the shorter first, reserved words and functions before the rest */
	CHECK(!strncmp(shown(e), "as abs ", 7));
	feed(e, KEY_ESC), core_free(e);
	e = typed("p.sql", "sel");
	CHECK(!strcmp(shown(e), "select self selective"));
	feed(e, KEY_ESC), core_free(e);
	e = typed("p.sql", "in");
	CHECK(!strcmp(e->pum[0].word, "in") && !strcmp(e->pum[0].kind, "Keyword"));
	feed(e, 'n');
	CHECK(!strcmp(e->pum[0].word, "inner"));
	feed(e, KEY_ESC), core_free(e);
	e = typed("p.sql", "pu");
	CHECK(!strcmp(e->pum[e->npum - 1].word, "pure"));
	feed(e, KEY_ESC), core_free(e);
	/* a keyword in the case typed: capitals stay capitals */
	e = typed("p.sql", "SELE");
	feed(e, 25);
	is(e, "SELECT\n");
	e = typed("p.sql", "Sele");
	feed(e, 25);
	is(e, "select\n");
	/* a word part, never a letter inside one (nvim-cmp) */
	e = typed("p.sql", "set");
	CHECK(!strstr(shown(e), "-"));
	feed(e, KEY_ESC), core_free(e);
	/* in a snippet Tab jumps, though a word typed in a placeholder opens
	   the popup */
	e = typed("p.sql", "two");
	feed(e, 25);
	feed(e, 's'), feed(e, 'e');
	CHECK(e->pum);
	feed(e, KEY_TAB);
	CHECK(!e->pum);
	feed(e, 'x');
	is(e, "se = x\n");
	/* none in a comment or a string */
	e = typed("p.sql", "-- sel");
	CHECK(!e->pum);
	feed(e, KEY_ESC), core_free(e);
	e = typed("p.sql", "'sel");
	CHECK(!e->pum);
	feed(e, KEY_ESC), core_free(e);
	/* an empty word starts again: s, Backspace, then @ offers variables */
	snprintf(t, sizeof t, "%sset |", file);
	e = at(t, "s\177@c");
	CHECK(e->pum && !strcmp(e->pum[0].word, "@count"));
	feed(e, KEY_ESC), core_free(e);
	/* [x] needs its brackets with a space in x; a column of two tables
	   once; FROM @: table variables; after a schema its tables, the
	   schema no table */
	snprintf(t, sizeof t, "create table a ([My Col] int, total int)\ncreate table b (total int)\nselect | from a join b on 1 = 1");
	e = at(t, "My");
	feed(e, 25);
	size_t n;
	const unsigned char *line = core_line(e, e->win->line, &n);
	CHECK(n > 15 && !memcmp(line, "select [My Col] ", 16));
	feed(e, KEY_ESC), core_free(e);
	e = at(t, "tota");
	CHECK(!strcmp(shown(e), "total"));
	feed(e, KEY_ESC), core_free(e);
	snprintf(t, sizeof t, "%sselect * from |", file);
	e = at(t, "@");
	CHECK(!strcmp(shown(e), "@w"));
	feed(e, KEY_ESC), core_free(e);
	e = at(t, "dbo");
	CHECK(!e->pum);
	feed(e, '.');
	CHECK(!strcmp(shown(e), "customers"));
	feed(e, KEY_ESC), core_free(e);
	/* names typed a moment ago: the word's statement is read when it
	   starts */
	e = at("create table orders (id int, total int)\n |", "select * from orders o where o.");
	CHECK(!strcmp(shown(e), "id total"));
	feed(e, KEY_ESC), core_free(e);
	/* a word's list is the one made when it started */
	e = at("select * from |\n", "g");
	CHECK(!core_edit(e, 15, 0, " join gq x", 10));
	feed(e, 'q');
	CHECK(!e->pum);
	feed(e, KEY_ESC), core_free(e);
	/* the file's names come from its index, made again once typing pauses
	   (HOLD): until then a name deleted elsewhere still shows */
	e = at("create table gone (id int)\nselect * from |", "g");
	CHECK(!strcmp(shown(e), "gone"));
	feed(e, KEY_ESC);
	typing_pause();   /* the timer armed before the first word */
	CHECK(!core_edit(e, 0, 27, "", 0));
	feed(e, 'g'), feed(e, 'g'), feed(e, 'A');
	for (const char *k = " join g"; *k; k++) feed(e, *k);
	CHECK(strstr(shown(e), "gone"));
	feed(e, KEY_ESC);
	typing_pause();
	feed(e, 'g'), feed(e, 'g'), feed(e, 'A');
	for (const char *k = " join g"; *k; k++) feed(e, *k);
	CHECK(!strstr(shown(e), "gone"));
	feed(e, KEY_ESC), core_free(e);
	/* a pause after WHERE (the index made again, its statement ending at
	   WHERE) keeps the statement's columns, as without one */
	e = at("select o.cust from orders o where |", "");
	typing_pause();
	feed(e, 'c');
	CHECK(strstr(shown(e), "cust"));
	feed(e, KEY_ESC), core_free(e);
	/* the review's cases: a word at its statement's end; FROM and a
	   column list end with their statement; a name once however often
	   used; @@ words no variables, and after @ the file's variables first,
	   no other words; DECLARE @t TABLE's columns; any case for a #temp;
	   FROM a, b; Q.name after SET is no schema; DELETE: tables and words;
	   a list made again after Esc */
	snprintf(t, sizeof t, "%sselect * from log l where l|", file);
	e = at(t, ".");
	CHECK(!strcmp(shown(e), "at msg"));
	feed(e, KEY_ESC), core_free(e);
	e = at("select 1 from t\nselect a, zz\nselect * from orders\nselect * from orders\nselect * from |\n", "z");
	CHECK(!e->pum);
	feed(e, KEY_BS), feed(e, 'o');
	CHECK(!strcmp(shown(e), "orders"));
	feed(e, KEY_ESC), core_free(e);
	e = at("declare @a_long_variable_name int, @count int\nselect @@rowcount\nset |\n", "@");
	CHECK(!strcmp(e->pum[0].kind, "Variable") && !strcmp(e->pum[1].kind, "Variable") && !strcmp(e->pum[2].kind, "Function"));
	for (int i = 0; i < e->npum; i++) CHECK(e->pum[i].word[0] == '@' && (strcmp(e->pum[i].word, "@@rowcount") || !strcmp(e->pum[i].kind, "Function")));
	feed(e, KEY_ESC), core_free(e);
	snprintf(t, sizeof t, "%sselect @w|", file);
	e = at(t, ".");
	CHECK(!strcmp(shown(e), "a"));
	feed(e, KEY_ESC), core_free(e);
	snprintf(t, sizeof t, "%sselect #T|", file);
	e = at(t, ".");
	CHECK(!strcmp(shown(e), "id name"));
	feed(e, KEY_ESC), core_free(e);
	e = at("create table b (k int)\nselect * from a, b x where x|\n", ".");
	CHECK(!strcmp(shown(e), "k"));
	feed(e, KEY_ESC), core_free(e);
	e = at("update t set x = 1 where t.y = 2\nselect * from |\n", "y");
	CHECK(!e->pum);
	feed(e, KEY_ESC), core_free(e);
	e = at("create table fact (id int)\ndelete |\n", "f");
	CHECK(strstr(shown(e), " fact ") && strstr(shown(e), " from "));
	feed(e, KEY_ESC), core_free(e);
	e = at("select * from |\n", "q");
	CHECK(!e->pum);
	feed(e, KEY_ESC);
	CHECK(!core_edit(e, 15, 0, " join qq x", 10));
	feed(e, 'a'), feed(e, 'q');
	CHECK(!strcmp(shown(e), "qq"));
	feed(e, KEY_ESC), core_free(e);
	/* the index's statements move with the text: a statement moved back
	   still holds its aliases */
	e = at("create table gone (id int)\nselect * from gone x where |\n", "z");
	feed(e, KEY_ESC);
	CHECK(!core_edit(e, 0, 27, "", 0));
	feed(e, 'g'), feed(e, 'g'), feed(e, 'A');
	for (const char *k = " x."; *k; k++) feed(e, *k);
	CHECK(!strcmp(shown(e), "z id"));   /* z: a bare name of a statement with one table, its column */
	feed(e, KEY_ESC), core_free(e);
	/* a bare name is a column of its statement's one table (Davy: d. after
	   join dom d showed nothing); not an alias, a variable, a function, Q
	   of Q.x, or a name in a statement of two tables */
	e = at("select name, x as total, count(*) cnt, isnull(y, 0) z, w tot, try_convert(int, t) from dbo.dom with (nolock)\n"
	       "where kind = @k order by name offset 0 rows fetch next 5 rows only\n"
	       "select n from app, dom\nupdate dom set a = 1, b = 2\nselect v = u from dom\n"
	       "select id, status from dom where kind = value order by id desc offset 10 rows\n"
	       "merge dom using app on 1 = 1 when matched then delete\nselect * from app a join dom d on d|\n", ".");
	CHECK(!strcmp(shown(e), "a b t u w x y id kind name value status"));
	feed(e, KEY_ESC), core_free(e);
	/* T-SQL's words the file uses rank with the reserved ones */
	e = at("declare @x int\nselect |\n", "in");
	CHECK(!strcmp(e->pum[1].word, "int"));
	feed(e, KEY_ESC), core_free(e);
	/* a statement's aliases are its own, an IF's condition is one */
	e = at("select * from orders o\nselect o.zz from t\nselect * from orders x where x|\n", ".");
	CHECK(!e->pum);
	feed(e, KEY_ESC), core_free(e);
	snprintf(t, sizeof t, "%sif exists (select 1 from log l where o| = 1) select * from customers o", file);
	e = at(t, ".");
	CHECK(!e->pum);
	feed(e, KEY_ESC), core_free(e);
	/* the index is not made again under an open popup, whose words are
	   in its pool */
	e = at("create table orders (id int)\nselect * from |\n", "or");
	CHECK(!strcmp(shown(e), "orders"));
	CHECK(!core_edit(e, 13, 0, "aaaa_", 5));   /* orders' place in the pool moves */
	typing_pause();
	feed(e, 25);
	line = core_line(e, e->win->line, &n);
	CHECK(n == 20 && !memcmp(line, "select * from orders", 20));
	feed(e, KEY_ESC), core_free(e);
	/* many names: the pool grows under the walk, and under a statement's
	   bare names becoming its table's columns */
	static char big[400000];
	int m = snprintf(big, sizeof big, "select ");
	for (int i = 0; i < 8000; i++) m += snprintf(big + m, sizeof big - (size_t)m, "bare_column_%d, ", i);
	snprintf(big + m, sizeof big - (size_t)m, "1 from some_table\nselect * from some_table t where t|");
	e = at(big, ".b");
	CHECK(e->npum == 8000);
	feed(e, KEY_ESC), core_free(e);
	m = snprintf(big, sizeof big, "select ");
	for (int i = 0; i < 8000; i++) m += snprintf(big + m, sizeof big - (size_t)m, "t.column_number_%d, ", i);
	snprintf(big + m, sizeof big - (size_t)m, "1 from some_table t where t|");
	e = at(big, ".c");
	CHECK(e->npum == 8000);
	feed(e, KEY_ESC), core_free(e);
	puts("test_snip: the popup offers by the statement: columns, tables, variables, words, snippets");
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
	const char *d, *b, *nm = snip_get(e, NULL, 0, 0, &d, &b);
	CHECK(nm && !strcmp(nm, "ghlp") && !strcmp(d, "h") && !strcmp(b, "H"));
	int i = 1;
	while (snip_get(e, NULL, 0, i, &d, &b)) i++;
	CHECK(i == total);
	CHECK(!strcmp(snip_get(e, "wr", 2, 0, &d, &b), "wrp") && !snip_get(e, "wr", 2, 1, &d, &b));
	is(e, "\n");
	/* ranked again for another buffer */
	e = typed("x_task_a.sql", "");
	CHECK(!strcmp(snip_get(e, "g", 1, 0, &d, &b), "gtsk"));
	CHECK(snip_insert(e, "nosuch") == -1);
	is(e, "\n");
	e = inserted("i.sql", "q", "", "pure");
	CHECK(e->mode == MODE_INSERT && e->win->col == 8);
	is(e, "SELECT 1q\n");
	e = inserted("i.sql", "x = 1", "$v", "wrp");
	is(e, "x = begin\n    1\nend\n");
	e = inserted("i.sql", "x", "v", "pure");   /* no $0: the area goes at the end */
	is(e, "SELECT 1x\n");
	/* never the text's last line break, as v$c */
	e = inserted("i.sql", "ab", "v$", "wrp");
	CHECK(core_lines(e) == 3);
	is(e, "begin\n    ab\nend\n");
	/* a block as its lines; a first line of blanks keeps them all */
	e = inserted("i.sql", "abc\ndef", "<C-v>j", "wrp");
	is(e, "begin\n    abc\n    def\nend\n");
	e = inserted("i.sql", "    \n    b", "Vj", "wrp");
	is(e, "    begin\n        \n        b\n    end\n");
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
	/* Enter at once takes the first: the file's group's */
	snprintf(path, sizeof path, "%s/x_proc_b.sql", dir);
	CHECK(!core_open(&pe, path));
	CHECK(!tools_pick(&pe, PICK_SNIPPETS) && tools_key(&pe, KEY_ENTER));
	is(&pe, "H\n");
	/* Enter takes the selected item, and a narrower prompt selects its
	   first again */
	snprintf(path, sizeof path, "%s/p.sql", dir);
	CHECK(!core_open(&pe, path));
	CHECK(!tools_pick(&pe, PICK_SNIPPETS) && tools_key(&pe, 'g') && tools_key(&pe, KEY_DOWN) && tools_key(&pe, KEY_ENTER));
	is(&pe, "H\n");
	CHECK(!core_open(&pe, path) && !tools_pick(&pe, PICK_SNIPPETS));
	for (int k = 0; k < 4; k++) CHECK(tools_key(&pe, KEY_DOWN));
	for (const char *k = "pure"; *k; k++) CHECK(tools_key(&pe, *k));
	CHECK(tools_key(&pe, KEY_ENTER));
	is(&pe, "SELECT 1\n");
	/* the popup's order with a prompt too: the group first, though dtz
	   is shorter */
	snprintf(path, sizeof path, "%s/x_task_b.sql", dir);
	CHECK(!core_open(&pe, path));
	CHECK(!tools_pick(&pe, PICK_SNIPPETS) && tools_key(&pe, 't') && tools_key(&pe, KEY_ENTER));
	is(&pe, "T\n");
	/* and the best match first (dtz starts with d, gdef comes first) */
	snprintf(path, sizeof path, "%s/p.sql", dir);
	CHECK(!core_open(&pe, path));
	CHECK(!tools_pick(&pe, PICK_SNIPPETS) && tools_key(&pe, 'd') && tools_key(&pe, KEY_ENTER));
	is(&pe, "C\n");
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
		               "```sql\n-- two : t\n${1:a} = ${2:b}\n```\n```sql\n-- wrp : w\nbegin\n\t$0\nend\n```\n"
		               "## 6. Task (default)\n```sql\n-- gtsk : t\nT\n```\n## 2. Default\n```sql\n-- gdef : d\nD\n```\n"
		               "## 3. Context (`@<task>_type`)\n```sql\n-- dtz : c\nC\n```\n"
		               "## 11. Shared helpers\n```sql\n-- ghlp : h\nH\n```\n");
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
		quiet();
		popup();
		groups();
		insert(n + 6);
		complete();
	}
	return 0;
}
