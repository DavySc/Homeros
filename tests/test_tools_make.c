/* :make (tools.h): the output as it comes in its buffer, the errors the
   compilers' formats name (gcc, rustc, Lean, dotnet; a missing file, a
   time of day and a URL are not errors), ]q [q and Enter on them, and a
   write running the command again while its buffer is shown. */
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "check.h"
#include "core.h"
#include "keys.h"
#include "loop.h"
#include "text.h"
#include "tools.h"

static Editor e;

/* runs the loop until the make buffer's last line is s */
static Buf *until(const char *s)
{
	Buf *b = NULL;
	for (int i = 0; i < 2000; i++) {
		for (Win *w = e.wins; w; w = w->next) if (w->buf->nonumber) b = w->buf;
		if (b) {
			size_t n = text_len(b->text), k = strlen(s);
			char t[256];
			if (n > k && n - k - 1 < n && k < sizeof t - 1) {
				text_read(b->text, n - k - 1, t, k + 1);
				if (!memcmp(t, s, k) && t[k] == '\n') return b;
			}
		}
		loop_once(10);
	}
	CHECK(!"the make buffer never ended in the line expected");
	return NULL;
}

static size_t lines_of(Buf *b) { return text_lines(b->text); }

/* how many times the counting command ran */
static int runs(void)
{
	FILE *f = fopen("runs", "r");
	int n = 0;
	for (int c; f && (c = getc(f)) != EOF; ) n += c == '\n';
	if (f) fclose(f);
	return n;
}

int main(void)
{
	char dir[] = "/tmp/tools_makeXXXXXX";
	CHECK(mkdtemp(dir) && !chdir(dir));
	FILE *f = fopen("a.c", "w");
	CHECK(f && fputs("int a;\nint b;\nint c;\n", f) >= 0 && !fclose(f));
	CHECK((f = fopen("B.cs", "w")) && fputs("class B {\n  int x;\n}\n", f) >= 0 && !fclose(f));
	/* a drive letter's colon is part of the path (C:\x on Windows; a
	   directory named C: here) */
	CHECK(!mkdir("C:", 0700) && (f = fopen("C:/d.c", "w")) && fputs("x\n", f) >= 0 && !fclose(f));
	CHECK((f = fopen("other.txt", "w")) && fputs("x\n", f) >= 0 && !fclose(f));
	CHECK(!core_open(&e, "other.txt"));

	CHECK(tools_qf(&e, 1) == -1 && strstr(e.msg, "E42"));
	CHECK(tools_make(&e, NULL) == -1 && strstr(e.msg, "E471"));
	/* the errors, and lines that name none */
	CHECK(!tools_make(&e, "printf 'at 12:30:45 started\\nsee http://x.org:80/y\\nnot.c:1:1: missing\\n"
	                      "a.c:2:5: error: no\\n --> a.c:3\\nB.cs(2,3): error CS1002: ; expected\\nC:/d.c:1:1: drive\\n'; exit 2"));
	Buf *b = until("Compilation exited abnormally with code 2");
	CHECK(e.buf->path && !strcmp(e.buf->path, "other.txt"));    /* the cursor stays in its window */
	CHECK(lines_of(b) == 11);

	/* ]q from the start: the first error, "(1 of 4)" */
	keys_input(&e, ']', 0);
	keys_input(&e, 'q', 0);
	CHECK(!strcmp(e.buf->path, "a.c") && e.win->line == 1 && e.win->col == 4 && strstr(e.msg, "(1 of 4): error: no"));
	CHECK(!tools_qf(&e, 1) && e.win->line == 2 && e.win->col == 0 && strstr(e.msg, "(2 of 4)"));
	CHECK(!tools_qf(&e, 1) && !strcmp(e.buf->path, "B.cs") && e.win->line == 1 && e.win->col == 2);
	CHECK(!tools_qf(&e, 1) && !strcmp(e.buf->path, "C:/d.c") && strstr(e.msg, "(4 of 4): drive"));
	CHECK(tools_qf(&e, 1) == -1 && strstr(e.msg, "E553"));
	for (const char *k = "3[q"; *k; k++) keys_input(&e, *k, 0);
	CHECK(!strcmp(e.buf->path, "a.c") && e.win->line == 1 && strstr(e.msg, "(1 of 4)"));

	/* Enter on the dotnet line of the make buffer opens it in the other window */
	CHECK(!core_win_goto(&e, 'j', 1) && e.buf == b);
	e.win->line = 7;     /* the command, a blank line, then the output: B.cs is its 6th line */
	CHECK(tools_key(&e, KEY_ENTER) && e.buf != b && !strcmp(e.buf->path, "B.cs") && e.win->line == 1);
	CHECK(!core_win_goto(&e, 'j', 1) && e.buf == b);
	e.win->line = 2;     /* a line that names no error: not taken */
	CHECK(!tools_key(&e, KEY_ENTER));
	CHECK(!core_win_goto(&e, 'p', 1));

	/* the one it replaces is stopped, and what it still says is not shown */
	CHECK(!tools_make(&e, "sleep 5"));
	CHECK(!tools_make(&e, "echo x >> runs; wc -l < runs"));
	b = until("Compilation finished");
	CHECK(lines_of(b) == 5 && runs() == 1);
	/* a write runs it again, on an emptied buffer and list */
	CHECK(!core_edit_file(&e, "other.txt", 0) && !core_write(&e, e.buf, 0));
	b = until("Compilation finished");
	CHECK(lines_of(b) == 5 && runs() == 2);    /* the old output went */
	for (int i = 0; i < 30; i++) loop_once(10);    /* sleep's end, if it came, said nothing */
	CHECK(lines_of(b) == 5);
	CHECK(tools_qf(&e, 1) == -1 && strstr(e.msg, "E42"));
	/* not once its window is closed */
	CHECK(!core_win_goto(&e, 'j', 1) && e.buf == b && !core_close(&e, 0));
	CHECK(!core_write(&e, e.buf, 0));
	for (int i = 0; i < 30; i++) loop_once(10);
	CHECK(runs() == 2);

	/* stderr shows too */
	CHECK(!tools_make(&e, "echo out; echo err >&2"));
	b = until("Compilation finished");
	CHECK(lines_of(b) == 6);

	/* :make alone runs the last command again, through the keys */
	CHECK(!core_win_goto(&e, 'j', 1) && e.buf == b && !core_close(&e, 0));
	for (const char *k = ":make\r"; *k; k++) keys_input(&e, *k, 0);
	b = until("Compilation finished");
	CHECK(lines_of(b) == 6 && e.wins->next);

	char rm[64];
	snprintf(rm, sizeof rm, "rm -rf %s", dir);
	CHECK(!system(rm));
	core_free(&e);
	printf("test_tools_make: ok\n");
	return 0;
}
