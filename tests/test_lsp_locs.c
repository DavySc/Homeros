/* Package 13 and its keys: the places a server names (lsp_locs), the keys
   that ask for them (C-] grr gri grt gO: what the server is asked, what is
   said, the list or the jump and C-o back) and the diagnostic jumps ("]d"
   "[d" "]D" "[D", C-o back), against what Neovim made of the same answers
   and diagnostics (tests/test_lsp_locs.py recorded them; it is also the
   fake server here, logging what it is asked).  usage: test_lsp_locs [FILE] */
#include <limits.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "check.h"
#include "core.h"
#include "file.h"
#include "keys.h"
#include "loop.h"
#include "lsp.h"
#include "tools.h"
#include "../src/lsp/json.h"

static Jtok *tk;
static int ntk;
static char line[1 << 20], dir[] = "/tmp/lsp_locsXXXXXX", sub[PATH_MAX + 32], logf[PATH_MAX + 32];

static char *str(const char *m, int i)
{
	char *b = malloc(tk[i].z - tk[i].a + 1);
	CHECK(b);
	json_str(m, tk, i, b);
	return b;
}

/* the places as one text, a line each: file, line, col, text, tab apart */
static Str got;
static int done;
static void places(void *ctx, const LspLoc *v, int n)
{
	(void)ctx;
	got.n = 0;
	str_add(&got, "", 0);
	for (int i = 0; i < n; i++)
		str_addf(&got, "%s\t%zu\t%zu\t%s\n", strncmp(v[i].path, sub, strlen(sub)) ? v[i].path : v[i].path + strlen(sub),
		         v[i].line + 1, v[i].col + 1, v[i].text);
	done = 1 + (n < 0);
}

/* keys typed, then (ask) the loop until the answer to what they asked has
   changed something, unless they said something at once (C-] without a
   keyword), 10 s at most */
static void type(Editor *e, const char *k, int ask)
{
	e->msg[0] = 0;
	for (const char *c = k; *c; c++) keys_input(e, (unsigned char)*c, c[1] != 0);
	core_changed = 0;
	for (int t = 0; ask && t < 1000 && !core_changed && !e->msg[0]; t++) loop_once(10);
}

/* the file, as Neovim's ":." (dir/ off), line and column after the keys */
static void here(const Editor *e)
{
	const char *p = e->buf->path ? e->buf->path : "";
	str_addf(&got, "%s %zu %zu\n", strncmp(p, dir, strlen(dir)) ? p : p + strlen(dir) + 1, e->win->line, e->win->col);
}

static char *log_read(size_t *n)
{
	char *t = NULL;
	FileId id;
	CHECK(!file_load(logf, &t, n, &id));
	return t;
}

static int indented;   /* a list's text had indent (the list shows it without) */

/* The case's key at its cursor, against what Neovim did: what the server
   was asked, what was said, the list and the cursor in it, or where the
   key went and where C-o goes back */
static int key_case(Editor *e, const char *m)
{
	int cur = json_get(m, tk, 0, "cur"), ls = json_get(m, tk, 0, "list"), caps = json_get(m, tk, 0, "caps");
	core_goto(e, (size_t)json_int(m, tk, json_at(tk, cur, 0)), (size_t)json_int(m, tk, json_at(tk, cur, 1)));
	char *k = str(m, json_get(m, tk, 0, "key")), *msg = str(m, json_get(m, tk, 0, "msg"));
	size_t n0, n1;
	free(log_read(&n0));
	type(e, strcmp(k, "<C-]>") ? k : "\x1d", 1);
	Str want = { 0 };
	str_add(&want, "", 0);
	for (int i = 0, x; (x = json_at(tk, json_get(m, tk, 0, "req"), i)) >= 0; i++) { char *r = str(m, x); str_addf(&want, "%s\n", r); free(r); }
	str_addf(&want, "%s\n", msg);
	char *l = log_read(&n1);
	got.n = 0;
	/* a file's info line, which :messages does not keep, aside; names
	   relative to the directory, as recorded */
	char *d = strstr(e->msg, dir);
	if (d) memmove(d, d + strlen(dir) + 1, strlen(d + strlen(dir)));
	str_addf(&got, "%.*s%s\n", (int)(n1 - n0), l + n0, e->msg[0] == '"' ? "" : e->msg);
	free(l);
	if (ls >= 0) {
		for (int i = 0, x; (x = json_at(tk, ls, i)) >= 0; i++) {
			char *f = str(m, json_at(tk, x, 0)), *t = str(m, json_at(tk, x, 3)), *u = t;
			indented |= *u == ' ' || *u == '\t';
			while (*u == ' ' || *u == '\t') u++;
			str_addf(&want, "%s:%ld:%ld: %s\n", f, json_int(m, tk, json_at(tk, x, 1)), json_int(m, tk, json_at(tk, x, 2)), u);
			free(f); free(t);
		}
		str_addf(&want, "%ld\n", json_int(m, tk, json_get(m, tk, 0, "row")));
		size_t len = text_len(e->buf->text);
		char *have = malloc(len + 1);
		CHECK(have && text_read(e->buf->text, 0, have, len) == len);
		str_addf(&got, "%.*s%zu\n", (int)len, have, e->win->line);
		free(have);
		CHECK(e->buf->nomodifiable && !core_close(e, 1));
	} else {
		for (int w = 0; w < 2; w++) {
			int a = json_get(m, tk, 0, w ? "back" : "at");
			char *f = str(m, json_at(tk, a, 0));
			str_addf(&want, "%s %ld %ld\n", f, json_int(m, tk, json_at(tk, a, 1)), json_int(m, tk, json_at(tk, a, 2)));
			free(f);
			here(e);
			if (!w) type(e, "\x0f", 0);
		}
	}
	if (caps >= 0) {
		char *c = str(m, caps), *t = NULL, init[sizeof logf + 8];
		size_t n;
		FileId id;
		snprintf(init, sizeof init, "%s.init", logf);
		CHECK(!file_load(init, &t, &n, &id));
		str_addf(&want, "%s\n", c);
		str_add(&got, t, n);
		free(c); free(t);
	}
	int bad = strcmp(got.s, want.s) != 0;
	if (bad) fprintf(stderr, "case %ld (%s):\nnvim:\n%score:\n%s", json_int(m, tk, json_get(m, tk, 0, "k")), k, want.s, got.s);
	str_free(&want);
	free(k); free(msg);
	return bad;
}

/* Once, on a case with places: SPC m a opens clangd's other file and C-o
   goes back; with a list in :make's window a write does not run the :make
   it showed before, after :make again it does */
static int keyed;
static void keyed_case(Editor *e)
{
	Buf *from = e->buf;
	size_t l0 = e->win->line, c0 = e->win->col;
	type(e, " ma", 1);
	CHECK(e->buf->path && !strcmp(e->buf->path + strlen(e->buf->path) - 4, "/o.c"));
	type(e, "\x0f", 0);
	CHECK(e->buf == from && e->win->line == l0 && e->win->col == c0);
	CHECK(!tools_make(e, "echo $$") && e->buf == from);
	for (int i = 0; i < 50; i++) loop_once(10);   /* its output, before grr's answer */
	type(e, "grr", 1);
	CHECK(e->buf != from && e->buf->nomodifiable);
	Buf *list = e->buf;
	size_t n = text_len(list->text);
	tools_written(e, from);
	for (int i = 0; i < 20; i++) loop_once(10);
	CHECK(text_len(list->text) == n);
	CHECK(!core_close(e, 1) && e->buf == from && !tools_make(e, NULL));
	for (int i = 0; i < 50; i++) loop_once(10);
	char *a = malloc(text_len(list->text) + 1), *b;
	CHECK(a && text_read(list->text, 0, a, text_len(list->text)) == text_len(list->text));
	a[text_len(list->text)] = 0;
	tools_written(e, from);
	for (int i = 0; i < 50; i++) loop_once(10);
	b = malloc(text_len(list->text) + 1);
	CHECK(b && text_read(list->text, 0, b, text_len(list->text)) == text_len(list->text));
	b[text_len(list->text)] = 0;
	CHECK(strstr(a, "echo $$") && strcmp(a, b));
	free(a); free(b);
	keyed = 1;
}

int main(int argc, char **argv)
{
	static Editor e;
	const char *path = argc > 1 ? argv[1] : "tests/test_lsp_locs.txt";
	static char py[PATH_MAX], txt[PATH_MAX], file[PATH_MAX + 64];
	CHECK(realpath("tests/test_lsp_locs.py", py) && realpath(path, txt) && mkdtemp(dir));
	static const char *const roots[] = { "lsproot", NULL };
	snprintf(logf, sizeof logf, "%s/log", dir);
	const char *sargv[] = { "python3", py, "--serve", txt, logf, NULL };
	LspServer table[2] = { { ".c", "c", sargv, roots, 0 }, { 0 } };
	lsp_servers = table;
	snprintf(file, sizeof file, "%s/lsproot", dir);
	FILE *o = fopen(file, "w");
	CHECK(o && !fclose(o) && (o = fopen(logf, "w")) && !fclose(o));
	FILE *f = fopen(path, "r");
	CHECK(f && !chdir(dir));   /* Neovim's: names in lists relative to it */
	int nloc = 0, ndiag = 0, bad = 0, nlist = 0;
	Str want = { 0 };
	while (fgets(line, sizeof line, f)) {
		size_t n = strlen(line);
		CHECK(json_parse(line, n, &tk, &ntk) > 0);
		long k = json_int(line, tk, json_get(line, tk, 0, "k"));
		int fs = json_get(line, tk, 0, "files");
		snprintf(sub, sizeof sub, "%s/%ld/", dir, k);
		CHECK(!mkdir(sub, 0700));
		for (int i = 1; i < 2 * tk[fs].n; i += 2) {
			int key = fs + 1;
			for (int j = 1; j < i; j++) key = tk[key].end;
			char *name = str(line, key), *body = str(line, tk[key].end);
			snprintf(file, sizeof file, "%s%s", sub, name);
			if (strchr(name, '/')) { *strrchr(file, '/') = 0; CHECK(!mkdir(file, 0700)); file[strlen(file)] = '/'; }
			CHECK((o = fopen(file, "wb")) && fputs(body, o) >= 0 && !fclose(o));
			free(name); free(body);
		}
		snprintf(file, sizeof file, "%sm.c", sub);
		CHECK(!core_open(&e, file) && e.buf->lsp);
		char *kind = str(line, json_get(line, tk, 0, "kind"));
		if (!strcmp(kind, "loc")) {
			want.n = 0;
			str_add(&want, "", 0);
			int it = json_get(line, tk, 0, "items");
			for (int i = 0, x; (x = json_at(tk, it, i)) >= 0; i++) {
				char *p = str(line, json_at(tk, x, 0)), *t = str(line, json_at(tk, x, 3));
				str_addf(&want, "%s\t%ld\t%ld\t%s\n", p, json_int(line, tk, json_at(tk, x, 1)), json_int(line, tk, json_at(tk, x, 2)), t);
				free(p); free(t);
			}
			char *m = str(line, json_get(line, tk, 0, "method"));
			done = 0;
			CHECK(!lsp_locs(e.buf->lsp, m, 0, !strcmp(m, "textDocument/definition"), places, NULL));
			for (int t = 0; t < 1000 && !done; t++) loop_once(10);
			CHECK(done == 1 + (json_get(line, tk, 0, "error") >= 0));
			if (done == 1 && strcmp(got.s, want.s) && bad++ < 5) fprintf(stderr, "case %ld (%s):\nnvim:\n%score:\n%s", k, m, want.s, got.s);
			nlist += json_get(line, tk, 0, "list") >= 0;
			bad += key_case(&e, line);
			int res = json_get(line, tk, 0, "result");   /* grr lists only a list */
			if (!keyed && res >= 0 && tk[res].type == 'a' && tk[it].n > 0 && json_get(line, tk, 0, "error") < 0) keyed_case(&e);
			free(m);
			nloc++;
		} else {
			int d = json_get(line, tk, 0, "diags"), have = 0, nd = tk[d].n;
			for (int t = 0; t < 1000 && have < nd; t++) { loop_once(10); lsp_diags(e.buf->lsp, &have); }
			CHECK(have == nd);
			int cur = json_get(line, tk, 0, "cur"), pos = json_get(line, tk, 0, "pos");
			core_goto(&e, (size_t)json_int(line, tk, json_at(tk, cur, 0)), (size_t)json_int(line, tk, json_at(tk, cur, 1)));
			e.msg[0] = 0;
			char *keys = str(line, json_get(line, tk, 0, "keys"));
			for (char *c = keys; *c; c++) keys_input(&e, (unsigned char)*c, c[1] != 0);
			long wl = json_int(line, tk, json_at(tk, pos, 0)), wc = json_int(line, tk, json_at(tk, pos, 1));
			int more = (int)json_int(line, tk, json_get(line, tk, 0, "more")), back = json_get(line, tk, 0, "back");
			size_t l1 = e.win->line, c1 = e.win->col;
			keys_input(&e, 0x0f, 0);   /* C-o: back where ]d left (setpcmark) */
			if ((l1 != (size_t)wl || c1 != (size_t)wc || more != !strstr(e.msg, "No more valid")
			     || e.win->line != (size_t)json_int(line, tk, json_at(tk, back, 0)) || e.win->col != (size_t)json_int(line, tk, json_at(tk, back, 1)))
			    && bad++ < 5)
				fprintf(stderr, "case %ld (%s): nvim %ld,%ld%s, core %zu,%zu%s, then C-o %zu,%zu\n", k, keys, wl, wc, more ? "" : " no more",
				        l1, c1, e.msg, e.win->line, e.win->col);
			free(keys);
			ndiag++;
		}
		free(kind);
		core_free(&e);
		memset(&e, 0, sizeof e);
	}
	fclose(f);
	str_free(&want);
	str_free(&got);
	CHECK(keyed && nlist && indented);
	if (bad) { printf("test_lsp_locs: %d of %d cases differ from Neovim\n", bad, nloc + ndiag); return 1; }
	printf("test_lsp_locs: %d answers and %d diagnostic jumps as Neovim's\n", nloc, ndiag);
	return 0;
}
