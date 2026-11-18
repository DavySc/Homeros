/* gq.  Its SQL hooks, as Davy's Neovim has them (conform.nvim with
   sqldiff): gq on a .sql buffer formats the operator's lines through
   sqlfmt's range (the whole text formatted, only the changes in those
   lines applied), :Format the whole buffer, and :w, :wq and :x format
   first when a sqldiff.toml is in the file's directory or one above it.
   The expected text is sqlfmt's own edit applied by hand, so this checks
   the wiring; test_sqlfmt checks the formatting against SQLDiff.  gq on
   other buffers is Neovim's, checked by the recorded traces; here only
   that a long paragraph's undo stays linear in its size. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "check.h"
#include "core.h"
#include "keys.h"
#include "sqlfmt.h"

static const char SRC[] = "select a,b from t where x=1\nSELECT  c   from u\nselect d from v\n";
static char dir[] = "/tmp/keys_gqXXXXXX", sub[128], file[160], toml[160];

static void keys(Editor *e, const char *s)
{
	for (; *s; s++) keys_feed(e, *s == '\n' ? KEY_ENTER : *s == 27 ? KEY_ESC : (unsigned char)*s, s[1] != 0);
}

static char *text(Editor *e)
{
	static char b[4096];
	size_t n = text_read(e->buf->text, 0, b, sizeof b - 1);
	b[n] = 0;
	return b;
}

/* SRC with sqlfmt's edit for bytes [a, z) of it */
static char *want(size_t a, size_t z)
{
	static char b[4096];
	Text *t = text_new(SRC, sizeof SRC - 1);
	const char *r;
	size_t n;
	CHECK(t && sqlfmt(t, &a, &z, &r, &n) == 0);
	memcpy(b, SRC, a);
	memcpy(b + a, r, n);
	strcpy(b + a + n, SRC + z);
	text_free(t);
	return b;
}

/* the bytes an undo puts back and takes out */
static size_t undone;
static void count(void *ctx, const Text *t, size_t pos, size_t ndel, const char *ins, size_t nins)
{
	(void)ctx; (void)t; (void)pos; (void)ins;
	undone += ndel + nins;
}

static void load(Editor *e, const char *path)
{
	FILE *f = fopen(path, "w");
	CHECK(f && fputs(SRC, f) >= 0 && !fclose(f));
	CHECK(!core_open(e, path));
}

static char *disk(const char *path)
{
	static char b[4096];
	FILE *f = fopen(path, "r");
	CHECK(f);
	size_t n = fread(b, 1, sizeof b - 1, f);
	b[n] = 0;
	fclose(f);
	return b;
}

int main(void)
{
	CHECK(mkdtemp(dir));
	snprintf(sub, sizeof sub, "%s/sub", dir);
	CHECK(!mkdir(sub, 0700));
	snprintf(file, sizeof file, "%s/a.sql", sub);
	snprintf(toml, sizeof toml, "%s/sqldiff.toml", dir);
	size_t l1 = strchr(SRC, '\n') - SRC + 1, l2 = strchr(SRC + l1, '\n') - SRC + 1;
	Editor e;

	/* gq over lines: gqq, j gqq, gqj, V j gq, v $ gq (the line break the
	   selection takes adds no line: op_on_lines()), gqG; u gives the text back */
	struct { const char *k; size_t a, z; } c[] = {
		{ "gqq", 0, l1 }, { "jgqq", l1, l2 }, { "gqj", 0, l2 }, { "Vjgq", 0, l2 }, { "v$gq", 0, l1 }, { "jgqG", l1, sizeof SRC - 1 },
	};
	for (size_t i = 0; i < sizeof c / sizeof *c; i++) {
		load(&e, file);
		keys(&e, c[i].k);
		CHECK(!strcmp(text(&e), want(c[i].a, c[i].z)));
		CHECK(strcmp(text(&e), SRC));
		keys(&e, "u");
		CHECK(!strcmp(text(&e), SRC));
		core_free(&e);
	}

	/* :Format, the whole buffer */
	load(&e, file);
	keys(&e, ":Format\n");
	CHECK(!strcmp(text(&e), want(0, sizeof SRC - 1)));
	core_free(&e);

	/* not a .sql buffer: gq does not use sqlfmt, :Format does nothing */
	char txt[176];
	snprintf(txt, sizeof txt, "%s/a.txt", sub);
	load(&e, txt);
	keys(&e, ":Format\n");
	CHECK(!strcmp(text(&e), SRC));
	core_free(&e);

	/* :w with no sqldiff.toml above the file writes it as it is */
	load(&e, file);
	keys(&e, "x:w\n");
	CHECK(!strcmp(disk(file), SRC + 1));
	core_free(&e);

	/* a sqldiff.toml two directories up: :w, :wq and :x format first */
	FILE *f = fopen(toml, "w");
	CHECK(f && !fclose(f));
	const char *w[] = { ":w\n", ":wq\n", ":x\n" };
	for (int i = 0; i < 3; i++) {
		load(&e, file);
		keys(&e, "ix\033x");     /* modified, so :x writes */
		keys(&e, w[i]);
		CHECK(!strcmp(disk(file), want(0, sizeof SRC - 1)));
		core_free(&e);
	}
	/* :x on an unmodified buffer writes nothing, so it formats nothing */
	load(&e, file);
	keys(&e, ":x\n");
	CHECK(!strcmp(disk(file), SRC));
	core_free(&e);
	/* a .txt next to it is never formatted */
	load(&e, txt);
	keys(&e, "ix\033x:w\n");
	CHECK(!strcmp(disk(txt), SRC));
	core_free(&e);

	/* a relative path finds the sqldiff.toml above the working directory */
	CHECK(!chdir(sub));
	load(&e, "a.sql");
	keys(&e, ":Format\n");
	keys(&e, "u:w\n");
	CHECK(!strcmp(disk("a.sql"), want(0, sizeof SRC - 1)));
	core_free(&e);

	/* gqG on a paragraph of 3,000 lines: format_lines() breaks a joined
	   line once it passes three times textwidth, so each join copies a few
	   lines at most (the undo moves 5.2 times the text, measured once;
	   joining the whole paragraph first would move about 1,500 times) */
	FILE *p = fopen(txt, "w");
	CHECK(p);
	for (int i = 0; i < 3000; i++) fputs("lorem ipsum dolor sit amet consectetur adipiscing elit sed do eiusmod\n", p);
	CHECK(!fclose(p));
	CHECK(!core_open(&e, txt));
	size_t size = text_len(e.buf->text);
	keys(&e, "gqG");
	undo_hook(e.buf->undo, count, NULL);
	keys(&e, "u");
	CHECK(text_len(e.buf->text) == size && undone < 8 * size);
	core_free(&e);

	unlink(file); unlink(txt); unlink(toml); rmdir(sub); rmdir(dir);
	puts("test_keys_gq: ok");
	return 0;
}
