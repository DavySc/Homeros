/* remote.h on Windows, as Notepad++ does it.  A named mutex (Local\: this
   session's) guards "find the editor's window, else become the editor", so
   of launches started together (Explorer opening several selected files)
   one becomes the editor and the others wait for its window, then hand it
   their files one at a time: Remote.tla "ok".  The window is message-only
   (HWND_MESSAGE): FindWindowEx finds it, nothing shows it, and the
   backend's PeekMessage over every window of the thread reads it.
   WM_COPYDATA is sent, not posted, so a launch learns whether the editor
   took its file; when the editor closed meanwhile, the launch becomes the
   editor, still holding the turn. */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include "file.h"
#include "remote.h"

enum { TAG = 0x686f6d72 };   /* "homr": another program's WM_COPYDATA is not a file */
static const wchar_t CLASS[] = L"homeros-remote";
static HWND me;
static char **q;             /* the files taken, oldest first */
static size_t qn, qcap;

static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
	if (m != WM_COPYDATA) return DefWindowProcW(h, m, wp, lp);
	const COPYDATASTRUCT *cd = (const COPYDATASTRUCT *)lp;
	const char *s = cd->lpData;
	if (cd->dwData != TAG || !cd->cbData || s[cd->cbData - 1]) return 0;   /* a path ends in its NUL */
	if (qn == qcap) {
		size_t cap = qcap ? 2 * qcap : 4;
		char **n = realloc(q, cap * sizeof *q);
		if (!n) return 0;
		q = n;
		qcap = cap;
	}
	if (!(q[qn] = malloc(cd->cbData))) return 0;
	memcpy(q[qn++], s, cd->cbData);
	return 1;
}

/* file's full name (the editor runs elsewhere), "" for none; 1 if taken */
static int hand(HWND h, const char *file)
{
	char p[PATH_MAX] = "";
	DWORD pid;
	if (file && file_full(file, p)) return 0;
	/* a launch the user started may bring another program forward
	   (SetForegroundWindow's rules): the editor raises the new frame */
	if (GetWindowThreadProcessId(h, &pid)) AllowSetForegroundWindow(pid);
	COPYDATASTRUCT cd = { TAG, (DWORD)strlen(p) + 1, p };
	return SendMessageW(h, WM_COPYDATA, 0, (LPARAM)&cd) == 1;
}

int remote_open(const char *file)
{
	HANDLE m = CreateMutexW(NULL, TRUE, L"Local\\homeros-remote");
	int took = 0;
	if (!m) return 0;
	/* not the first: wait for our turn (WAIT_ABANDONED: its owner died
	   inside, and it is ours as well) */
	if (GetLastError() == ERROR_ALREADY_EXISTS && WaitForSingleObject(m, INFINITE) == WAIT_FAILED) { CloseHandle(m); return 0; }
	HWND h = FindWindowExW(HWND_MESSAGE, NULL, CLASS, NULL);
	if (h) took = hand(h, file);
	/* none, or it closed while we sent: this launch is the editor (one
	   still there refused it: run here, and the next launch finds it) */
	if (!took && !FindWindowExW(HWND_MESSAGE, NULL, CLASS, NULL)) {
		WNDCLASSW wc = { .lpfnWndProc = proc, .hInstance = GetModuleHandleW(NULL), .lpszClassName = CLASS };
		if (RegisterClassW(&wc)) me = CreateWindowExW(0, CLASS, NULL, 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, wc.hInstance, NULL);
	}
	ReleaseMutex(m);
	CloseHandle(m);
	return took;
}

char *remote_next(void)
{
	if (!qn) return NULL;
	char *s = q[0];
	memmove(q, q + 1, --qn * sizeof *q);
	return s;
}

void remote_stop(void)
{
	if (me) DestroyWindow(me), UnregisterClassW(CLASS, GetModuleHandleW(NULL));
	me = NULL;
	while (qn) free(q[--qn]);
	free(q);
	q = NULL;
	qcap = 0;
}
#endif
