/* Review of PR 25: what src/file/win.c does in cases its test does not cover.
   build and run (from the repo root, tools-env/bin on PATH):
     zig cc -target x86_64-windows-gnu -std=c17 -D_FILE_OFFSET_BITS=64 -Iinclude -O1 \
       -o probe.exe reviews/pr-25-repro/probe.c src/file/win.c src/file/journal.c src/text/text.c
     WINEDEBUG=-all WINEPREFIX=$PWD/build/win/prefix-review wine probe.exe */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "file.h"

static char dir[MAX_PATH], path[MAX_PATH + 32];
static const char *names[] = { "SAVE_OK", "SAVE_CHANGED", "SAVE_KEPT", "SAVE_ERR" };

static void put(const char *p, const char *s) { FILE *f = fopen(p, "wb"); fputs(s, f); fclose(f); }
static const char *get(const char *p)
{
	static char b[256];
	FILE *f = fopen(p, "rb");
	if (!f) return "(no file)";
	b[fread(b, 1, sizeof b - 1, f)] = 0;
	fclose(f);
	for (char *c = b; *c; c++) if (*c == '\n') *c = '|';
	return b;
}
static int save(const char *s, FileId *id, int force)
{
	Text *t = text_new(s, strlen(s));
	char kept[8];
	int r = file_save(path, t, strlen(s), id, force, kept, sizeof kept);
	text_free(t);
	return r;
}
static void load(FileId *id) { char *d; size_t n; file_load(path, &d, &n, id); free(d); }
static const char *attrs(void)
{
	static char b[64];
	DWORD a = GetFileAttributesA(path);
	snprintf(b, sizeof b, "%s%s%s", a & FILE_ATTRIBUTE_HIDDEN ? "hidden " : "", a & FILE_ATTRIBUTE_READONLY ? "readonly " : "",
	         a & FILE_ATTRIBUTE_SYSTEM ? "system " : "");
	return b;
}

int main(int argc, char **argv)
{
	FileId id;
	GetTempPathA(sizeof dir, dir);
	strcat(dir, "homeros-review");
	CreateDirectoryA(dir, NULL);
	snprintf(path, sizeof path, "%s\\f.txt", dir);
	char st[MAX_PATH + 32];
	snprintf(st, sizeof st, "XDG_STATE_HOME=%s\\state", dir);
	_putenv(st);

	if (argc > 1) {   /* the child of step 3: journals an unsaved change, then waits to be killed */
		Journal *j; Text *t = text_new("abc\n", 4), *rt; unsigned s; FileId rid;
		load(&id);
		if (journal_open(path, &j, &rt, &s, &rid)) return 1;
		journal_base(j, t, 0, &id);
		journal_edit(j, 0, 0, "unsaved ", 8, 0);
		journal_sync(j);
		Sleep(INFINITE);
	}

	/* 1. attributes of the file saved over */
	static const struct { const char *name; DWORD a; } at[] = { { "hidden", FILE_ATTRIBUTE_HIDDEN },
		{ "readonly", FILE_ATTRIBUTE_READONLY }, { "system", FILE_ATTRIBUTE_SYSTEM } };
	for (int i = 0; i < 3; i++) {
		SetFileAttributesA(path, FILE_ATTRIBUTE_NORMAL); DeleteFileA(path);
		put(path, "old\n");
		SetFileAttributesA(path, at[i].a);
		load(&id);
		int r = save("new\n", &id, 0);
		printf("1. %-8s file: plain save %s, errno %d; now \"%s\", attributes: %s\n", at[i].name, names[r], r == SAVE_ERR ? errno : 0, get(path), attrs());
		if (r == SAVE_ERR) {
			r = save("new\n", &id, 1);
			printf("   %-8s file: forced save %s; now \"%s\", attributes: %s\n", at[i].name, names[r], get(path), attrs());
		}
	}
	SetFileAttributesA(path, FILE_ATTRIBUTE_NORMAL); DeleteFileA(path);

	/* 2. another program has the file open (no delete sharing, as most do) */
	put(path, "old\n");
	load(&id);
	HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
	int r = save("new\n", &id, 0);
	printf("2. file held open by a reader: save %s, errno %d; file \"%s\"\n", names[r], r == SAVE_ERR ? errno : 0, get(path));
	r = save("new\n", &id, 1);
	printf("   forced: %s; file \"%s\"\n", names[r], get(path));
	WIN32_FIND_DATAA f;
	char pat[MAX_PATH + 32];
	snprintf(pat, sizeof pat, "%s\\*homeros-*", dir);
	printf("   temp left behind: %s\n", FindFirstFileA(pat, &f) == INVALID_HANDLE_VALUE ? "no" : f.cFileName);
	CloseHandle(h);

	/* 3. the journal left by a crash (a child that journals and is killed) is
	   being read by another program (a scanner, a backup) when the editor
	   opens the file again */
	Journal *j; Text *rt; unsigned s; FileId rid;
	DeleteFileA(path); put(path, "abc\n");
	char self[MAX_PATH], cmd[MAX_PATH + 16], jn[3 * MAX_PATH];
	GetModuleFileNameA(NULL, self, sizeof self);
	snprintf(cmd, sizeof cmd, "\"%s\" crash", self);
	STARTUPINFOA si = { .cb = sizeof si };
	PROCESS_INFORMATION pi;
	CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
	Sleep(1500);
	TerminateProcess(pi.hProcess, 9);
	WaitForSingleObject(pi.hProcess, 10000);
	snprintf(pat, sizeof pat, "%s\\state\\homeros-data\\journal\\*", dir);
	HANDLE fh = FindFirstFileA(pat, &f);
	while (fh != INVALID_HANDLE_VALUE && f.cFileName[0] == '.') if (!FindNextFileA(fh, &f)) break;
	snprintf(jn, sizeof jn, "%s\\state\\homeros-data\\journal\\%s", dir, f.cFileName);
	DWORD got = f.nFileSizeLow;
	HANDLE scanner = CreateFileA(jn, GENERIC_READ, 7, NULL, OPEN_EXISTING, 0, NULL);   /* shares everything */
	int jr1 = journal_open(path, &j, &rt, &s, &rid);
	printf("3. journal of %lu bytes left by a crash, read by another program with full sharing: journal_open %d%s\n", got, jr1,
	       jr1 < 0 ? errno == EWOULDBLOCK ? " (EWOULDBLOCK: the core says E303, another editor has it open)" : " (other error)" : "");
	CloseHandle(scanner);
	jr1 = journal_open(path, &j, &rt, &s, &rid);
	printf("   once that program closed it: journal_open %d (1 = recovered)\n", jr1);
	if (jr1 >= 0) journal_close(j);
	return 0;
}
