/* src/git/win.c, the Windows half of package 17:
   1. git_real: an existing path comes back full, '/'-separated, its drive
      letter upper case, its names as stored (on Windows only: Wine returns
      them as typed, measured 1 run); a missing one fails.
   2. The watch (Git.tla's Outside): a change in the work tree, a directory
      of it or .git counts; one under an ignored directory or in a nested repository's
      .git does not; one git_drain takes every change before it, not only
      the first batch (Git.tla ActsOnSeen: write.c's gate is one git_drain);
      a linked work tree's common directory outside the work tree is
      watched too; after git_unwatch nothing is seen.
   3. git_trash: the file goes to the Recycle Bin, not away.
   Runs on Windows (CI's windows job) or under Wine (src/git/check.sh);
   elsewhere it says so. */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "check.h"
#include "loop.h"
#include "../src/git/gitint.h"

/* git.c's other modules, which a watch never reaches */
int syntax_open(void) { return 0; }
void syntax_edit(void) {}
void syntax_free(void) {}
void syntax_faces(void) {}
int syntax_md(void) { return 0; }

static char top[MAX_PATH];

static void put(const char *rel, const char *s)
{
	char p[2 * MAX_PATH];
	snprintf(p, sizeof p, "%s/%s", top, rel);
	FILE *f = fopen(p, "wb");
	CHECK(f && fputs(s, f) >= 0);
	fclose(f);
}

static void dir(const char *rel)
{
	char p[2 * MAX_PATH];
	snprintf(p, sizeof p, "%s/%s", top, rel);
	CHECK(CreateDirectoryA(p, NULL));
}

/* Runs the loop until the repository's version moves or ms pass; then
   lets the rest of that change's notifications arrive and be taken */
static int seen(Repo *r, int ms)
{
	unsigned long v = r->ver;
	for (DWORD t0 = GetTickCount(); r->ver == v && GetTickCount() - t0 < (DWORD)ms; ) loop_once(50);
	int moved = r->ver != v;
	for (DWORD t0 = GetTickCount(); GetTickCount() - t0 < 300; ) loop_once(50);
	return moved;
}

int main(void)
{
	char tmp[MAX_PATH], low[MAX_PATH], out[PATH_MAX];
	CHECK(GetTempPathA(sizeof tmp, tmp));
	snprintf(top, sizeof top, "%shomeros-git-%lu-%lu", tmp, (unsigned long)GetCurrentProcessId(), (unsigned long)GetTickCount());
	CHECK(CreateDirectoryA(top, NULL));

	/* 1 */
	snprintf(low, sizeof low, "%s\\", top);
	low[0] = (char)(low[0] | 0x20);
	CHECK(!git_real(low, out));
	CHECK(out[0] >= 'A' && out[0] <= 'Z' && out[1] == ':' && !strchr(out, '\\'));
	/* the last name only: Windows may give the temporary directory by its
	   short (8.3) name, which git_real makes long */
	CHECK(out[strlen(out) - 1] != '/' && !strcmp(strrchr(out, '/') + 1, strrchr(top, '\\') + 1));
	snprintf(top, sizeof top, "%s", out);
	snprintf(low, sizeof low, "%s/missing", top);
	CHECK(git_real(low, out));
	if (!GetProcAddress(GetModuleHandleA("ntdll.dll"), "wine_get_version")) {
		dir("MixedCase");
		snprintf(low, sizeof low, "%s/mixedcase", top);
		CHECK(!git_real(low, out) && !strcmp(out + strlen(top), "/MixedCase"));
	}

	/* 2: status listed build/ as ignored */
	Repo *r = calloc(1, sizeof *r);
	CHECK(r);
	r->top = top;
	char common[2 * MAX_PATH];
	snprintf(common, sizeof common, "%s/.git", top);
	r->common = common;
	str_add(&r->ignp, "build/", 7);
	size_t at = 0;
	r->ign = &at; r->nign = 1;
	r->busy = 1;   /* git_schedule starts nothing */
	git_watch(r);
	CHECK(r->wt);
	/* made after the watch: Wine reports changes in a subdirectory only if
	   it saw it made (measured, 1 run); Windows watches the whole tree
	   (ReadDirectoryChangesW's bWatchSubtree, as Microsoft documents it) */
	dir(".git"); dir("build"); dir("sub"); dir("sub/.git");
	seen(r, 1000);
	put("a.txt", "a\n");
	CHECK(seen(r, 5000));
	put(".git/HEAD", "ref: refs/heads/main\n");
	CHECK(seen(r, 5000));
	put("sub/y.txt", "y");
	CHECK(seen(r, 5000));
	put("build/x.o", "x");
	CHECK(!seen(r, 1000));
	put("sub/.git/index", "x");
	CHECK(!seen(r, 1000));
	put("a.txt", "b\n");
	CHECK(seen(r, 5000));
	/* a batch whose first entries do not count: Wine packs the later
	   ones unaligned (measured, 1 run).  The first change completes the
	   pending read; the rest come while none is pending, so they wait
	   for the next one, together (measured, 5 runs); three rounds, as
	   one was not enough on CI's machine (1 run) */
	for (int k = 0; k < 3; k++) {
		put("build/v.o", "v");
		Sleep(200);
		put("build/w.o", "w"); put("build/z.o", "z"); put("a.txt", k & 1 ? "a\n" : "b\n");
		CHECK(seen(r, 5000));
	}
	put("build/y.o", "y");
	Sleep(200);
	put("a.txt", "a\n");
	Sleep(200);
	unsigned long v = r->ver;
	git_drain(r);
	CHECK(r->ver != v);
	seen(r, 300);
	git_unwatch(r);
	CHECK(!r->wt);
	put("a.txt", "c\n");
	CHECK(!seen(r, 1000));
	/* a linked work tree: its refs live in the common directory, elsewhere */
	dir("main.git");
	snprintf(common, sizeof common, "%s/main.git", top);
	char wt[2 * MAX_PATH];
	snprintf(wt, sizeof wt, "%s/wt", top);
	dir("wt");
	r->top = wt;
	git_watch(r);
	put("main.git/packed-refs", "x\n");
	CHECK(seen(r, 5000));
	git_unwatch(r);

	/* 3: the bin of the file's drive holds one more */
	wchar_t drive[] = { (wchar_t)top[0], L':', L'\\', 0 };
	SHQUERYRBINFO q0 = { 0 }, q1 = { 0 };
	q0.cbSize = q1.cbSize = sizeof q0;
	CHECK(SHQueryRecycleBinW(drive, &q0) == S_OK);
	char victim[2 * MAX_PATH], name[64];
	snprintf(name, sizeof name, "trash-%lu.txt", (unsigned long)GetCurrentProcessId());
	put(name, "t\n");
	snprintf(victim, sizeof victim, "%s/%s", top, name);
	CHECK(!git_trash(victim));
	CHECK(GetFileAttributesA(victim) == INVALID_FILE_ATTRIBUTES);
	CHECK(SHQueryRecycleBinW(drive, &q1) == S_OK && q1.i64NumItems == q0.i64NumItems + 1);
	puts("test_git_win: paths full and '/'-separated; the watch sees the work tree, .git and a linked work tree's common directory but not ignored or nested repositories, one drain takes all, and it stops; the trash is the Recycle Bin");
	return 0;
}
#else
#include <stdio.h>
int main(void) { puts("test_git_win: Windows only (src/git/check.sh runs it under Wine)"); return 0; }
#endif
