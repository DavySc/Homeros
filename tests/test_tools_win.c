/* The recent picker on Windows (src/tools/check.sh runs it under Wine):
   paths with drive letters and '\', as file_full and GetCurrentDirectory give
   them, are shown relative to the cwd, a file open both as a buffer and in
   v:oldfiles is listed once, the current buffer and homeros' own files are
   left out, and the frecency file is written and read back (the smart
   picker ranks a visited file first). */
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "check.h"
#include "core.h"
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
	core_draw(&e, &g);
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
	put("a.txt", "a\n"); put("b.txt", "b\n"); put("sub\\c.txt", "c\n"); put("sub\\d.txt", "d\n");
	snprintf(st, sizeof st, "XDG_STATE_HOME=%s\\state", d);
	CHECK(!_putenv(st));
	CHECK(!_putenv("XDG_DATA_HOME="));   /* the frecency into %LOCALAPPDATA%: set it here */
	snprintf(st, sizeof st, "LOCALAPPDATA=%s\\state", d);
	CHECK(!_putenv(st));
	/* v:oldfiles: b (also a buffer below, typed with '/'), sub\c, a file of
	   homeros' own, and a (the current buffer) */
	snprintf(f, sizeof f, "9 %s\\b.txt\n8 %s\\sub\\c.txt\n7 %s\\sub\\d.txt\n6 %s\\state\\homeros-data\\x\n5 %s\\a.txt\n", d, d, d, d, d);
	snprintf(st, sizeof st, "%s\\state\\homeros-data\\oldfiles", d);
	_mkdir("state\\homeros-data");
	put(st, f);
	/* sub\d visited often: its frecency deadline far ahead */
	snprintf(f, sizeof f, "1e12 %s\\sub\\d.txt\n", d);
	snprintf(st, sizeof st, "%s\\state\\homeros-data\\picker-frecency", d);
	put(st, f);
	CHECK(!shada_read());
	snprintf(f, sizeof f, "%c:/%s/b.txt", d[0], d + 3);
	CHECK(!core_open(&e, f));
	CHECK(!core_edit_file(&e, "a.txt", 0));
	CHECK(!grid_resize(&g, 40, 140));
	CHECK(!tools_pick(&e, PICK_RECENT));
	rows(got, sizeof got);
	if (strcmp(got, "b.txt|sub\\c.txt|sub\\d.txt|")) { fprintf(stderr, "recent: %s\n", got); CHECK(0); }
	tools_key(&e, KEY_ESC);
	/* smart, "sub": the same match, so the frecency decides */
	CHECK(!tools_pick(&e, PICK_SMART));
	for (const char *k = "sub"; *k; k++) CHECK(tools_key(&e, *k));
	rows(got, sizeof got);
	if (strcmp(got, "sub\\d.txt|sub\\c.txt|")) { fprintf(stderr, "smart: %s\n", got); CHECK(0); }
	tools_key(&e, KEY_ESC);
	tools_key(&e, KEY_ESC);
	/* the picker's first open visits the open buffers: the frecency file */
	snprintf(st, sizeof st, "%s\\state\\homeros-data\\picker-frecency", d);
	FILE *fp = fopen(st, "rb");
	CHECK(fp);
	size_t n = fread(got, 1, sizeof got - 1, fp);
	got[n] = 0;
	fclose(fp);
	CHECK(strstr(got, "\\a.txt\n") && strstr(got, "\\b.txt\n") && strstr(got, "\\sub\\d.txt\n"));
	grid_free(&g);
	core_free(&e);
	printf("test_tools_win: recent files relative to C:\\..., listed once, frecency written and read\n");
	return 0;
}
#else
#include <stdio.h>
int main(void) { puts("test_tools_win: Windows only (src/tools/check.sh runs it under Wine)"); return 0; }
#endif
