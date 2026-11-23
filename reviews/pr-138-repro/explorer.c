/* PR 138's review, on real Windows: does proc_open (explorer FILE) open the
   file the infoview's child names?  serve.c joins %TEMP% and the name with
   "/", so the path has both kinds of slash.  A file type of its own
   (.hvprobe, this user only) is opened by this program (--handler), which
   notes the path and how its window is to be shown.
     a  proc_open, backslashes only          (the control)
     b  proc_open, %TEMP% "/" name           (what serve.c writes)
     c  proc_open, forward slashes only
     d  ShellExecute, %TEMP% "/" name        (the alternative)
   Build: as src/infoview/check.sh's test, plus -lshell32 -ladvapi32. */
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <string.h>
#include "loop.h"

static char mark[MAX_PATH];

static int seen(const char *name)
{
	for (ULONGLONG end = GetTickCount64() + 8000; GetTickCount64() < end; ) {
		loop_once(50);
		char b[4096] = "";
		FILE *f = fopen(mark, "rb");
		if (f) { b[fread(b, 1, sizeof b - 1, f)] = 0; fclose(f); }
		char *l = strstr(b, name);
		if (l) {
			while (l > b && l[-1] != '\n') l--;
			printf("  opened: %.*s\n", (int)strcspn(l, "\r\n"), l);
			return 1;
		}
	}
	return 0;
}

static void touch(const char *p) { FILE *f = fopen(p, "wb"); if (f) { fputs("x", f); fclose(f); } else printf("  cannot make %s\n", p); }

int main(int argc, char **argv)
{
	char tmp[MAX_PATH], p[MAX_PATH + 64], cmd[2 * MAX_PATH + 64], self[MAX_PATH];
	GetTempPathA(MAX_PATH - 64, tmp);
	tmp[strlen(tmp) - 1] = 0;                 /* as %TEMP%: no slash at the end */
	snprintf(mark, sizeof mark, "%s\\hvprobe-mark.txt", tmp);
	if (argc == 3 && !strcmp(argv[1], "--handler")) {
		STARTUPINFOA si;
		GetStartupInfoA(&si);
		FILE *f = fopen(mark, "ab");
		if (f) { fprintf(f, "%s flags=%lx show=%u\n", argv[2], si.dwFlags, si.wShowWindow); fclose(f); }
		return 0;
	}
	DeleteFileA(mark);
	GetModuleFileNameA(NULL, self, MAX_PATH);
	snprintf(cmd, sizeof cmd, "\"%s\" --handler \"%%1\"", self);
	if (RegSetKeyValueA(HKEY_CURRENT_USER, "Software\\Classes\\.hvprobe", NULL, REG_SZ, "hvprobe.file", 13) ||
	    RegSetKeyValueA(HKEY_CURRENT_USER, "Software\\Classes\\hvprobe.file\\shell\\open\\command", NULL, REG_SZ, cmd, (DWORD)strlen(cmd) + 1)) {
		puts("explorer probe: cannot register the file type");
		return 0;
	}
	SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
	printf("TEMP is %s\n", tmp);

	snprintf(p, sizeof p, "%s\\hv-a.hvprobe", tmp); touch(p);
	printf("a backslashes, proc_open %d: %s\n", proc_open(p), p);
	printf("a: %s\n", seen("hv-a.hvprobe") ? "OPENED" : "NOT OPENED");

	snprintf(p, sizeof p, "%s/hv-b.hvprobe", tmp); touch(p);
	printf("b serve.c's, proc_open %d: %s\n", proc_open(p), p);
	printf("b: %s\n", seen("hv-b.hvprobe") ? "OPENED" : "NOT OPENED");

	snprintf(p, sizeof p, "%s/hv-c.hvprobe", tmp); touch(p);
	for (char *s = p; *s; s++) if (*s == '\\') *s = '/';
	printf("c forward, proc_open %d: %s\n", proc_open(p), p);
	printf("c: %s\n", seen("hv-c.hvprobe") ? "OPENED" : "NOT OPENED");

	snprintf(p, sizeof p, "%s/hv-d.hvprobe", tmp); touch(p);
	printf("d ShellExecute %d: %s\n", (int)(INT_PTR)ShellExecuteA(NULL, "open", p, NULL, NULL, SW_SHOWNORMAL), p);
	printf("d: %s\n", seen("hv-d.hvprobe") ? "OPENED" : "NOT OPENED");

	RegDeleteTreeA(HKEY_CURRENT_USER, "Software\\Classes\\.hvprobe");
	RegDeleteTreeA(HKEY_CURRENT_USER, "Software\\Classes\\hvprobe.file");
	/* explorer windows the failed cases opened (Documents) stay; the runner is thrown away */
	puts("explorer probe: done");
	return 0;
}
