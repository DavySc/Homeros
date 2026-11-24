/* What an open FindFirstChangeNotification handle on a folder keeps other
   programs from doing (review of PR 131).  Prints each call's error code
   with the handle open and after it is closed.
   zig cc -target x86_64-windows-gnu -o probe.exe probe.c */
#include <windows.h>
#include <stdio.h>

static char top[MAX_PATH];
static void mk(const char *p, char *out, size_t n)
{
	snprintf(out, n, "%s\\%s", top, p);
	if (!CreateDirectoryA(out, NULL)) printf("mkdir %s: %lu\n", p, GetLastError());
}
static void say(const char *what, BOOL ok) { printf("%-44s %s (%lu)\n", what, ok ? "ok" : "FAILS", ok ? 0 : GetLastError()); }

int main(void)
{
	char tmp[MAX_PATH], p[MAX_PATH], s[MAX_PATH], q[MAX_PATH], t[MAX_PATH], r[MAX_PATH], r2[MAX_PATH], u[MAX_PATH], v[MAX_PATH];
	GetTempPathA(sizeof tmp, tmp);
	snprintf(top, sizeof top, "%shomeros-probe-%lu-%lu", tmp, GetCurrentProcessId(), GetTickCount());
	CreateDirectoryA(top, NULL);
	printf("build %lu\n", (unsigned long)(GetVersion() >> 16));
	const DWORD f = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_ATTRIBUTES;

	/* 1: remove the watched folder, then its parent */
	mk("p", p, sizeof p); mk("p\\s", s, sizeof s);
	HANDLE h = FindFirstChangeNotificationA(s, FALSE, f);
	say("open: RemoveDirectory(watched)", RemoveDirectoryA(s));
	say("open: RemoveDirectory(parent)", RemoveDirectoryA(p));
	say("open: CreateDirectory(watched) again", CreateDirectoryA(s, NULL));
	FindCloseChangeNotification(h);
	RemoveDirectoryA(s);
	say("closed: RemoveDirectory(parent)", RemoveDirectoryA(p));

	/* 2: rename the watched folder */
	mk("q", q, sizeof q); mk("q\\t", t, sizeof t);
	snprintf(v, sizeof v, "%s\\q\\t2", top);
	h = FindFirstChangeNotificationA(t, FALSE, f);
	say("open: MoveFile(watched)", MoveFileA(t, v));
	FindCloseChangeNotification(h);

	/* 3: rename the parent of the watched folder */
	mk("r", r, sizeof r); mk("r\\w", u, sizeof u);
	snprintf(r2, sizeof r2, "%s\\r2", top);
	h = FindFirstChangeNotificationA(u, FALSE, f);
	say("open: MoveFile(parent)", MoveFileA(r, r2));
	FindCloseChangeNotification(h);
	say("closed: MoveFile(parent)", MoveFileA(r, r2) || GetFileAttributesA(r2) != INVALID_FILE_ATTRIBUTES);
	return 0;
}
