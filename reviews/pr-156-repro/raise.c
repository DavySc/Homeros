/* PR 156 review probe: may the editor bring its new frame forward when a
   launch started from the program in front hands it a file?  Roles:
   (none)  the driver: runs rounds with and without AllowSetForegroundWindow
   "E"     the editor stand-in: a visible window, a message-only window; on
           WM_COPYDATA (after the launch has gone) it opens a second window
           and calls SetForegroundWindow, as backend_win's win_open + raise;
           exit code 1 if that window is then the foreground window
   "X a"   the program in front: takes the foreground (exit 2 if it cannot),
           starts "L a" and waits; exits 0
   "L a"   the launch: a = 1 calls AllowSetForegroundWindow(E's pid), then
           sends WM_COPYDATA and exits */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int handed;
static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
	if (m == WM_COPYDATA) { handed = 1; return 1; }
	return DefWindowProcW(h, m, wp, lp);
}
static void pump(DWORD ms)
{
	MSG m;
	for (DWORD t0 = GetTickCount(); GetTickCount() - t0 < ms; ) {
		MsgWaitForMultipleObjects(0, NULL, FALSE, 10, QS_ALLINPUT);
		while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) DispatchMessageW(&m);
	}
}
static HANDLE run(const char *args)
{
	char self[MAX_PATH], line[MAX_PATH + 32];
	STARTUPINFOA si = { .cb = sizeof si };
	PROCESS_INFORMATION pi;
	GetModuleFileNameA(NULL, self, sizeof self);
	snprintf(line, sizeof line, "\"%s\" %s", self, args);
	if (!CreateProcessA(NULL, line, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) return NULL;
	CloseHandle(pi.hThread);
	return pi.hProcess;
}
static DWORD code(HANDLE p) { DWORD c = 99; WaitForSingleObject(p, 20000); GetExitCodeProcess(p, &c); CloseHandle(p); return c; }
static HWND top(const wchar_t *cls, const wchar_t *title)
{
	HWND h = CreateWindowExW(0, cls, title, WS_OVERLAPPEDWINDOW, 50, 50, 400, 300, NULL, NULL, GetModuleHandleW(NULL), NULL);
	ShowWindow(h, SW_SHOWNORMAL);
	return h;
}
int main(int argc, char **argv)
{
	WNDCLASSW wc = { .lpfnWndProc = proc, .hInstance = GetModuleHandleW(NULL), .lpszClassName = L"probe-raise" };
	WNDCLASSW wm = wc; wm.lpszClassName = L"probe-raise-msg";
	RegisterClassW(&wc); RegisterClassW(&wm);
	if (argc == 2 && !strcmp(argv[1], "E")) {
		top(L"probe-raise", L"E1");
		CreateWindowExW(0, L"probe-raise-msg", NULL, 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, wc.hInstance, NULL);
		SetEvent(OpenEventW(EVENT_MODIFY_STATE, FALSE, L"Local\\probe-raise-ready"));
		for (int i = 0; i < 1500 && !handed; i++) pump(10);
		if (!handed) return 3;
		pump(300);                                   /* the launch has exited */
		HWND w2 = top(L"probe-raise", L"E2");
		SetForegroundWindow(w2);
		pump(300);
		return GetForegroundWindow() == w2;
	}
	if (argc == 3 && !strcmp(argv[1], "X")) {
		HWND x = top(L"probe-raise", L"X");
		SetForegroundWindow(x);
		pump(300);
		if (GetForegroundWindow() != x) {
			/* a click on it, as the user would: synthesized input makes us the input's owner */
			RECT r; GetWindowRect(x, &r);
			SetCursorPos(r.left + 100, r.top + 100);
			INPUT in[2] = { { .type = INPUT_MOUSE, .mi.dwFlags = MOUSEEVENTF_LEFTDOWN }, { .type = INPUT_MOUSE, .mi.dwFlags = MOUSEEVENTF_LEFTUP } };
			SendInput(2, in, sizeof *in);
			pump(300);
		}
		if (GetForegroundWindow() != x) return 2;
		char a[8]; snprintf(a, sizeof a, "L %s", argv[2]);
		HANDLE l = run(a);
		while (MsgWaitForMultipleObjects(1, &l, FALSE, 10000, QS_ALLINPUT) == WAIT_OBJECT_0 + 1) pump(1);
		DWORD c = 99; GetExitCodeProcess(l, &c);
		pump(1500);                                  /* stay in front while E raises */
		return c == 0 ? 0 : 4;
	}
	if (argc == 3 && !strcmp(argv[1], "L")) {
		HWND h = FindWindowExW(HWND_MESSAGE, NULL, L"probe-raise-msg", NULL);
		DWORD pid;
		if (!h) return 5;
		if (argv[2][0] == '1' && GetWindowThreadProcessId(h, &pid) && !AllowSetForegroundWindow(pid)) return 6;
		COPYDATASTRUCT cd = { 1, 2, (void *)"x" };
		return SendMessageW(h, WM_COPYDATA, 0, (LPARAM)&cd) == 1 ? 0 : 7;
	}
	int rounds = argc > 1 ? atoi(argv[1]) : 5, front[2] = { 0, 0 }, n[2] = { 0, 0 };
	HANDLE ready = CreateEventW(NULL, FALSE, FALSE, L"Local\\probe-raise-ready");
	for (int i = 0; i < 2 * rounds; i++) {
		int a = i % 2;                                 /* alternating: without, with */
		HANDLE e = run("E");
		WaitForSingleObject(ready, 10000);
		Sleep(300);
		HANDLE x = run(a ? "X 1" : "X 0");
		DWORD cx = code(x), ce = code(e);
		printf("round %d, AllowSetForegroundWindow %s: front program's code %lu (0 ok, 2 could not take the foreground, 4 launch failed), editor's new frame in front: %lu\n", i / 2, a ? "called" : "not called", (unsigned long)cx, (unsigned long)ce);
		if (cx == 0 && ce < 2) { n[a]++; front[a] += (int)ce; }
		Sleep(300);
	}
	printf("raise: new frame in front without the call %d of %d, with it %d of %d\n", front[0], n[0], front[1], n[1]);
	return 0;
}
