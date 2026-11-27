/* The recent picker on Windows (src/tools/check.sh runs it under Wine):
   paths with drive letters and '\', as file_full and GetCurrentDirectory give
   them, are shown relative to the cwd, a file open both as a buffer and in
   v:oldfiles is listed once, the current buffer and homeros' own files are
   left out, and the frecency file is written and read back (the smart
   picker ranks a visited file first), the files picker never runs
   find.exe, and :make's errors name C:\ paths.  The terminal (Ctrl+.)
   runs cmd.exe on a pseudo-console. */
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "check.h"
#include "core.h"
#include "loop.h"
#include "shada.h"
#include "tools.h"
static Editor e;
static Grid g;

static void put(const char *f, const char *s)
{
	FILE *fp = fopen(f, "wb");
	CHECK(fp && fputs(s, fp) >= 0 && !fclose(fp));
}

/* the list as drawn, rows joined by '|' (as tests/test_tools_pick.c reads it) */
static void rows(char *out, size_t cap)
{
	size_t n = 0;
	out[0] = 0;
	core_draw(&e, NULL, &g);
	tools_draw(&e, &g);
	for (int r = 0; r < g.rows; r++) {
		char line[1024];
		size_t k = 0;
		for (int c = 0; c < g.cols && k + 4 < sizeof line; c++) {
			Cell *x = &g.c[r * g.cols + c];
			memcpy(line + k, x->ch, x->len); k += x->len;
		}
		line[k] = 0;
		char *s = strstr(line, "\xe2\x94\x82"), *z;
		if (strstr(line, "\xe2\x94\x9c") || !s || !(z = strstr(s + 3, "\xe2\x94\x82"))) continue;
		s += 3; *z = 0;
		while (*s == ' ') s++;
		while (z > s && z[-1] == ' ') *--z = 0;
		if (!*s || strstr(s, "> ") == s) continue;
		/* past the file icon and its space */
		char *sp = strchr(s, ' ');
		if (sp) s = sp + 1;
		n += (size_t)snprintf(out + n, cap - n, "%s|", s);
	}
}

int main(void)
{
	char d[MAX_PATH], f[3 * MAX_PATH], st[3 * MAX_PATH], got[4096];
	CHECK(GetTempPathA(sizeof d, d));
	strcat(d, "homeros-pick");
	_mkdir(d);
	CHECK(!_chdir(d));
	CHECK(_getcwd(d, sizeof d));
	_mkdir("sub");
	_mkdir("state");
	put("a.txt", "a\n"); put("b.txt", "b\n"); put("sub\\zqc.txt", "c\n"); put("sub\\zqd.txt", "d\n");
	snprintf(st, sizeof st, "XDG_STATE_HOME=%s\\state", d);
	CHECK(!_putenv(st));
	CHECK(!_putenv("XDG_DATA_HOME="));   /* the frecency into %LOCALAPPDATA%: set it here */
	snprintf(st, sizeof st, "LOCALAPPDATA=%s\\state", d);
	CHECK(!_putenv(st));
	/* v:oldfiles: b (also a buffer below, typed with '/'), sub\zqc and zqd, a file of
	   homeros' own, and a (the current buffer) */
	snprintf(f, sizeof f, "9 %s\\b.txt\n8 %s\\sub\\zqc.txt\n7 %s\\sub\\zqd.txt\n6 %s\\state\\homeros-data\\x\n5 %s\\a.txt\n", d, d, d, d, d);
	snprintf(st, sizeof st, "%s\\state\\homeros-data\\oldfiles", d);
	_mkdir("state\\homeros-data");
	put(st, f);
	/* sub\zqd visited often: its frecency deadline far ahead */
	snprintf(f, sizeof f, "1e12 %s\\sub\\zqd.txt\n", d);
	snprintf(st, sizeof st, "%s\\state\\homeros-data\\picker-frecency", d);
	put(st, f);
	CHECK(!shada_read());
	snprintf(f, sizeof f, "%c:/%s/b.txt", d[0], d + 3);
	CHECK(!core_open(&e, f));
	CHECK(!core_edit_file(&e, "a.txt", 0));
	CHECK(!grid_resize(&g, 40, 140));
	CHECK(!tools_pick(&e, PICK_RECENT));
	rows(got, sizeof got);
	if (strcmp(got, "b.txt|sub\\zqc.txt|sub\\zqd.txt|")) { fprintf(stderr, "recent: %s\n", got); CHECK(0); }
	tools_key(&e, KEY_ESC);
	/* smart, "zq" (in no temp directory's name): the same match, so the
	   frecency decides */
	CHECK(!tools_pick(&e, PICK_SMART));
	for (const char *k = "zq"; *k; k++) CHECK(tools_key(&e, *k));
	rows(got, sizeof got);
	if (strcmp(got, "sub\\zqd.txt|sub\\zqc.txt|")) { fprintf(stderr, "smart: %s\n", got); CHECK(0); }
	tools_key(&e, KEY_ESC);
	tools_key(&e, KEY_ESC);
	/* the picker's first open visits the open buffers: the frecency file */
	snprintf(st, sizeof st, "%s\\state\\homeros-data\\picker-frecency", d);
	FILE *fp = fopen(st, "rb");
	CHECK(fp);
	size_t n = fread(got, 1, sizeof got - 1, fp);
	got[n] = 0;
	fclose(fp);
	CHECK(strstr(got, "\\a.txt\n") && strstr(got, "\\b.txt\n") && strstr(got, "\\sub\\zqd.txt\n"));
	/* files with neither fd nor rg on PATH: Wine's find.exe, like Windows',
	   searches text, so no finder is run */
	CHECK(!_putenv("PATH=C:\\windows\\system32"));
	tools_pick(&e, PICK_FILES);
	rows(got, sizeof got);
	if (got[0] || strcmp(e.msg, "No supported finder found: `fd` `fdfind` `rg`")) { fprintf(stderr, "files: %s / %s\n", got, e.msg); CHECK(0); }
	tools_key(&e, KEY_ESC);
	/* :make through cmd.exe: dotnet's C:\...\file(line,col), its drive
	   letter's colon part of the path */
	snprintf(f, sizeof f, "echo %s\\sub\\zqc.txt(1,1): error CS1002: ; expected", d);
	CHECK(!tools_make(&e, f));
	for (int i = 0; i < 500 && tools_qf(&e, 1); i++) loop_once(10);
	if (!strstr(e.buf->path, "zqc.txt") || e.win->line || e.win->col || !strstr(e.msg, "(1 of 1): error CS1002")) { fprintf(stderr, "make: %s / %s\n", e.buf->path, e.msg); CHECK(0); }
	/* a quoted path reaches cmd.exe as typed (not the CRT's \"), and the
	   CR of each CR LF is not the message's */
	put("sp ace.txt", "sub\\zqd.txt(1,1): error CS3: quoted\r\n");
	CHECK(!tools_make(&e, "type \"sp ace.txt\""));
	for (int i = 0; i < 500 && tools_qf(&e, 1); i++) loop_once(10);
	if (!strstr(e.buf->path, "zqd.txt") || strcmp(strstr(e.msg, "(1 of 1)"), "(1 of 1): error CS3: quoted")) { fprintf(stderr, "make quoted: %s / %s\n", e.buf->path, e.msg); CHECK(0); }
	/* the terminal: cmd.exe (COMSPEC) answers what it is typed, and exit
	   closes it */
	CHECK(tools_key(&e, KEY_CTRL_DOT));
	for (const char *k = "echo zq%OS%\n"; *k; k++) CHECK(tools_key(&e, *k == '\n' ? KEY_ENTER : (unsigned char)*k));
	for (int i = 0; i < 100 && (rows(got, sizeof got), !strstr(got, "zqWindows_NT|")); i++) loop_once(100);
	if (!strstr(got, "zqWindows_NT|")) { fprintf(stderr, "terminal: %s\n", got); CHECK(0); }
	for (const char *k = "exit\n"; *k; k++) tools_key(&e, *k == '\n' ? KEY_ENTER : (unsigned char)*k);
	for (int i = 0; i < 100 && (rows(got, sizeof got), strstr(got, "zqWindows_NT|")); i++) loop_once(100);
	CHECK(!strstr(got, "zqWindows_NT|"));   /* its exit closed the float */
	grid_free(&g);
	core_free(&e);
	printf("test_tools_win: recent files relative to C:\\..., listed once, frecency written and read; no find.exe; :make's C:\\ errors, quoted paths; the terminal runs cmd.exe\n");
	return 0;
}
#else
#include <stdio.h>
int main(void) { puts("test_tools_win: Windows only (src/tools/check.sh runs it under Wine)"); return 0; }
#endif
