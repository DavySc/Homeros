/* gq.  Its SQL hooks, as Davy's Neovim has them (conform.nvim with
   sqldiff): gq on a .sql buffer formats the operator's lines through
   sqlfmt's range (the whole text formatted, only the changes in those
   lines applied), :Format the whole buffer, and :w, :wq and :x format
   first when a sqldiff.toml is in the file's directory or one above it.
   The expected text is sqlfmt's own edit applied by hand, so this checks
   the wiring; test_sqlfmt checks the formatting against SQLDiff.  gq on
   other buffers is Neovim's, checked by the recorded traces; here only
   that a long paragraph's undo stays linear in its size.  SystemVerilog
   and Verilog go through verible-verilog-format the same way (Davy's
   4-space indent; --lines for gq, the whole text for :Format and :w,
   which formats under a .slang directory); a fake on PATH indents the
   lines it is given and records its arguments.  A formatter that fails,
   hangs or is missing changes nothing.  Zig goes through zig fmt in-process
   (syntax_zig_fmt; test_syntax_zig checks it against zig fmt) on every :w
   and :Format; a file that does not parse is written as it is (the
   write's message is the one shown, so :Format says why). */
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

static const char SV[] = "module m;\n  wire a;\n  wire b;\nendmodule\n";
static char fake[200], args[200];

/* MODE ok: indents the wires in lines a-b (all without --lines) by 4 and
   writes to stderr, as Verible does with exit 0; fail: exits 1; hang:
   sleeps */
static void fake_as(const char *mode)
{
	FILE *f = fopen(fake, "w");
	CHECK(f && fprintf(f, "#!/bin/sh\necho \"$*\" > %s\ncase %s in fail) exit 1;; hang) exec sleep 10;; esac\n"
	                     "echo \"verible's notes\" >&2\nr=${2#--lines=}; [ \"$r\" = - ] && r=1-999999\n"
	                     "awk -v a=${r%%-*} -v b=${r#*-} 'NR>=a&&NR<=b{sub(/^  wire/,\"    wire\")}1'\n", args, mode) > 0 && !fclose(f));
	CHECK(!chmod(fake, 0700));
}

static void verilog(void)
{
	char bin[160], sv[176], slang[160], path[4096];
	Editor e;
	snprintf(bin, sizeof bin, "%s/bin", dir);
	snprintf(fake, sizeof fake, "%s/verible-verilog-format", bin);
	snprintf(args, sizeof args, "%s/args", dir);
	snprintf(sv, sizeof sv, "%s/m.sv", sub);
	snprintf(slang, sizeof slang, "%s/.slang", dir);
	snprintf(path, sizeof path, "%s:%s", bin, getenv("PATH"));
	CHECK(!mkdir(bin, 0700) && !setenv("PATH", path, 1));
	fake_as("ok");
	FILE *f = fopen(sv, "w");
	CHECK(f && fputs(SV, f) >= 0 && !fclose(f));
	/* gq on line 3: only it, --lines=3-3; u gives the text back */
	CHECK(!core_open(&e, sv));
	keys(&e, "jjgqq");
	CHECK(!strcmp(text(&e), "module m;\n  wire a;\n    wire b;\nendmodule\n"));
	CHECK(!strcmp(disk(args), "--indentation_spaces=4 --lines=3-3 -\n"));
	keys(&e, "u");
	CHECK(!strcmp(text(&e), SV));
	/* :Format: the whole text, no --lines; the cursor below the edit stays
	   on its character */
	keys(&e, "G$:Format\n");
	CHECK(!strcmp(text(&e), "module m;\n    wire a;\n    wire b;\nendmodule\n"));
	CHECK(e.win->line == 3 && e.win->col == 8);
	CHECK(!strcmp(disk(args), "--indentation_spaces=4 -\n"));
	core_free(&e);
	/* :w formats only under a .slang directory */
	CHECK(!core_open(&e, sv));
	keys(&e, "ix\033x:w\n");
	CHECK(!strcmp(disk(sv), SV));
	CHECK(!mkdir(slang, 0700));
	keys(&e, "ix\033x:w\n");
	CHECK(!strcmp(disk(sv), "module m;\n    wire a;\n    wire b;\nendmodule\n"));
	core_free(&e);
	/* every Verilog name goes to verible, as Davy's conform.nvim's
	   systemverilog and verilog */
	const char *ext[] = { "svh", "v", "vh" };
	for (int i = 0; i < 3; i++) {
		snprintf(path, sizeof path, "%s/m.%s", sub, ext[i]);
		CHECK((f = fopen(path, "w")) && fputs(SV, f) >= 0 && !fclose(f) && !core_open(&e, path));
		keys(&e, ":Format\n");
		CHECK(!strcmp(text(&e), "module m;\n    wire a;\n    wire b;\nendmodule\n"));
		core_free(&e);
		unlink(path);
	}
	/* failing, hanging (killed after 1 s) and missing: nothing changes */
	f = fopen(sv, "w");
	CHECK(f && fputs(SV, f) >= 0 && !fclose(f));
	const char *bad[] = { "fail", "hang" };
	for (int i = 0; i < 3; i++) {
		if (i < 2) fake_as(bad[i]); else unlink(fake);
		CHECK(!core_open(&e, sv));
		keys(&e, ":Format\n");
		CHECK(!strcmp(text(&e), SV) && strstr(e.msg, "verible-verilog-format"));
		core_free(&e);
	}
	unlink(sv); unlink(args); rmdir(slang); rmdir(bin);
}

static void zig(void)
{
	static const char Z[] = "const a=1;\nfn f() void {\nreturn;\n}\n", F[] = "const a = 1;\nfn f() void {\n    return;\n}\n";
	char zf[160];
	snprintf(zf, sizeof zf, "%s/a.zig", sub);
	FILE *f = fopen(zf, "w");
	CHECK(f && fputs(Z, f) >= 0 && !fclose(f));
	Editor e;
	/* :w formats, with no marker file; the cursor stays on its character;
	   u gives the text back; a mark on a line the format leaves stays (the
	   edit is only the bytes that differ) */
	CHECK(!core_open(&e, zf));
	keys(&e, "Gkix\033x:w\n");
	CHECK(!strcmp(disk(zf), F) && !strcmp(text(&e), F));
	CHECK(e.win->line == 2 && e.win->col == 4);
	keys(&e, "u");
	CHECK(!strcmp(text(&e), Z));
	keys(&e, ":Format\njjI  \033");
	e.buf->mark[0] = (Mark){ 1, 0, 1 };
	keys(&e, ":w\n");
	CHECK(!strcmp(text(&e), F) && e.buf->mark[0].set && e.buf->mark[0].line == 1);
	core_free(&e);
	/* a text that does not parse is written as it is; :Format says why */
	f = fopen(zf, "w");
	CHECK(f && fputs("fn f( {\n", f) >= 0 && !fclose(f));
	CHECK(!core_open(&e, zf));
	keys(&e, "ix\033x:w\n");
	CHECK(!strcmp(disk(zf), "fn f( {\n"));
	keys(&e, ":Format\n");
	CHECK(!strcmp(text(&e), "fn f( {\n") && strstr(e.msg, "zig fmt"));
	core_free(&e);
	unlink(zf);
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

	/* the cursor after sqlfmt's edit stays inside the buffer and on its
	   text: below the edit it moves with it, inside it keeps its offset
	   (reviews/pr-67.md finding A: G :Format left it past the last line,
	   and x then deleted a huge byte count) */
	static const char *gap = "select 1\n\n\n\n\n\n\n\nSELECT   2,   3 FROM t\n";
	const char *after[] = { "G:Format\nx", "G:Format\nD", "G:Format\nvd", "7GgqGx", "8GgqjD", "G:w\nx" };
	for (int i = 0; i < 6; i++) {
		FILE *g = fopen(file, "w");
		CHECK(g && fputs(gap, g) >= 0 && !fclose(g));
		CHECK(!core_open(&e, file));
		keys(&e, after[i]);
		CHECK(e.win->line < core_lines(&e) && e.win->col <= core_line_len(&e, e.win->line));
		core_free(&e);
	}
	/* lines formatted above the cursor: it stays on "select d" */
	load(&e, file);
	keys(&e, "2j:Format\n");
	size_t ln;
	const unsigned char *cl = core_line(&e, e.win->line, &ln);
	CHECK(e.win->col == 0 && ln >= 8 && !memcmp(cl, "select d", 8));
	core_free(&e);
	/* below the edit: the same character of the line under it */
	FILE *f2 = fopen(file, "w");
	CHECK(f2 && fputs("SELECT  c   from u\n-- end\n", f2) >= 0 && !fclose(f2));
	CHECK(!core_open(&e, file));
	keys(&e, "j$:Format\n");
	cl = core_line(&e, e.win->line, &ln);
	CHECK(ln == 6 && !memcmp(cl, "-- end", 6) && e.win->col == 5);
	core_free(&e);

	/* gqG on a paragraph of 3,000 lines: format_lines() breaks a joined
	   line once it passes three times textwidth, so each join copies a few
	   lines at most (the undo moves 7.2 times the text with the lines gq saves
	   first, measured once;
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
	CHECK(text_len(e.buf->text) == size && undone < 10 * size);
	core_free(&e);

	verilog();
	zig();
	unlink(file); unlink(txt); unlink(toml); rmdir(sub); rmdir(dir);
	puts("test_keys_gq: ok");
	return 0;
}
