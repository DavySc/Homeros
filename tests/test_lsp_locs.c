/* Package 13 and its keys: the places a server names (lsp_locs) and the
   diagnostic jumps ("]d" "[d" "]D" "[D"), against what Neovim made of the
   same answers and diagnostics (tests/test_lsp_locs.py recorded them; it
   is also the fake server here).  usage: test_lsp_locs [FILE] */
#include <limits.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "check.h"
#include "core.h"
#include "keys.h"
#include "loop.h"
#include "lsp.h"
#include "tools.h"
#include "../src/lsp/json.h"

static Jtok *tk;
static int ntk;
static char line[1 << 20], dir[] = "/tmp/lsp_locsXXXXXX", sub[PATH_MAX + 32];

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

/* keys typed, then the loop until what they asked has come */
static void type(Editor *e, const char *k)
{
	core_changed = 0;
	for (const char *c = k; *c; c++) keys_input(e, (unsigned char)*c, c[1] != 0);
	for (int t = 0; t < 1000 && !core_changed; t++) loop_once(10);
}

/* The keys over the places got: SPC m a opens clangd's other file and
   C-] the first place, C-o going back from each; grr lists them all in
   :make's window, which becomes current, each line "file:line:col: text"
   without the text's indent, and a write does not run the :make that
   window showed before.  1 checked. */
static int keyed_case(Editor *e, int n, const char *m)
{
	if (strcmp(m, n > 1 ? "textDocument/references" : "textDocument/definition")) return 0;
	size_t l0 = e->win->line, c0 = e->win->col, line, col;
	Buf *from = e->buf;
	char f[PATH_MAX + 32];
	CHECK(sscanf(got.s, "%[^\t]\t%zu\t%zu", f, &line, &col) == 3);
	if (n == 1) {
		type(e, " ma");
		CHECK(e->buf->path && !strcmp(e->buf->path + strlen(e->buf->path) - 4, "/o.c"));
		type(e, "\x0f");
		CHECK(e->buf == from && e->win->line == l0 && e->win->col == c0);
		type(e, "\x1d");
		CHECK(e->win->line == line - 1 && e->win->col <= col - 1 && strstr(e->buf->path, f));
		type(e, "\x0f");
		CHECK(e->buf == from && e->win->line == l0 && e->win->col == c0);
		return 1;
	}
	CHECK(!tools_make(e, "echo made") && e->buf == from);
	type(e, "grr");
	CHECK(e->buf != from && e->buf->nomodifiable);
	Str t = { 0 };
	for (const char *g = got.s; *g; g = strchr(g, '\n') + 1) {
		const char *a = strchr(g, '\t'), *b = strchr(a + 1, '\t'), *c = strchr(b + 1, '\t'), *x = c + 1;
		while (*x == ' ' || *x == '\t') x++;
		str_addf(&t, "%s%.*s:%.*s:%.*s: %.*s\n", sub, (int)(a - g), g, (int)(b - a - 1), a + 1, (int)(c - b - 1), b + 1,
		         (int)(strchr(x, '\n') - x), x);
	}
	size_t len = text_len(e->buf->text);
	char *have = malloc(len + 1);
	CHECK(have && text_read(e->buf->text, 0, have, len) == len);
	have[len] = 0;
	tools_written(e, from);
	for (int i = 0; i < 20; i++) loop_once(10);
	free(have);
	CHECK(text_len(e->buf->text) == len && (have = malloc(len + 1)) && text_read(e->buf->text, 0, have, len) == len);
	have[len] = 0;
	if (strcmp(have, t.s)) { fprintf(stderr, "grr's list:\n%swanted:\n%s", have, t.s); exit(1); }
	free(have);
	str_free(&t);
	return 1;
}

int main(int argc, char **argv)
{
	static Editor e;
	const char *path = argc > 1 ? argv[1] : "tests/test_lsp_locs.txt";
	static char py[PATH_MAX], txt[PATH_MAX], file[PATH_MAX + 64];
	CHECK(realpath("tests/test_lsp_locs.py", py) && realpath(path, txt) && mkdtemp(dir));
	static const char *const roots[] = { "lsproot", NULL };
	const char *sargv[] = { "python3", py, "--serve", txt, NULL };
	LspServer table[2] = { { ".c", "c", sargv, roots, 0 }, { 0 } };
	lsp_servers = table;
	snprintf(file, sizeof file, "%s/lsproot", dir);
	FILE *o = fopen(file, "w");
	CHECK(o && !fclose(o));
	FILE *f = fopen(path, "r");
	CHECK(f);
	int nloc = 0, ndiag = 0, bad = 0, keyed[2] = { 0 };
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
			CHECK(!lsp_locs(e.buf->lsp, m, 0, places, NULL));
			for (int t = 0; t < 1000 && !done; t++) loop_once(10);
			CHECK(done == 1);
			if (strcmp(got.s, want.s) && bad++ < 5) fprintf(stderr, "case %ld (%s):\nnvim:\n%score:\n%s", k, m, want.s, got.s);
			int ni = tk[it].n;
			if (!keyed[ni > 1] && ni > 0 && !strcmp(got.s, want.s)) keyed[ni > 1] = keyed_case(&e, ni, m);
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
			int more = (int)json_int(line, tk, json_get(line, tk, 0, "more"));
			if ((e.win->line != (size_t)wl || e.win->col != (size_t)wc || more != !strstr(e.msg, "No more valid")) && bad++ < 5)
				fprintf(stderr, "case %ld (%s): nvim %ld,%ld%s, core %zu,%zu%s\n", k, keys, wl, wc, more ? "" : " no more",
				        e.win->line, e.win->col, e.msg);
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
	CHECK(keyed[0] && keyed[1]);
	if (bad) { printf("test_lsp_locs: %d of %d cases differ from Neovim\n", bad, nloc + ndiag); return 1; }
	printf("test_lsp_locs: %d answers and %d diagnostic jumps as Neovim's\n", nloc, ndiag);
	return 0;
}
