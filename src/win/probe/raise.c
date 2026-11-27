/* Build with remote.c and what it links, as src/win/check.sh links
   test_win_remote (-luser32 -lshell32); run on a Windows desktop:
   raise.exe [rounds].  Checks what no automated test can (DESIGN.md, "One
   editor per Windows session"): that a frame raised for a file another
   program opened comes to the front.  A stand-in editor becomes the editor
   and raises its window as win_raise does for each file it takes; for each
   round a stand-in for "the program in front" takes the foreground (asking,
   a zero mouse move, F24, else it waits for the user's click: Windows'
   default lock refuses the others), starts a launch that hands a file over,
   and reports which window is in front after.  Planting "AllowSetForegroundWindow
   dropped" in remote.c should leave the program in front. */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "remote.h"

static HWND mkwin(const char *title, int x)
{
	WNDCLASSA wc = { .lpfnWndProc = DefWindowProcA, .hInstance = GetModuleHandleA(NULL), .lpszClassName = "raise-probe",
	                 .hbrBackground = (HBRUSH)(COLOR_WINDOW + 1) };
	RegisterClassA(&wc);
	HWND h = CreateWindowExA(0, "raise-probe", title, WS_OVERLAPPEDWINDOW | WS_VISIBLE, x, 100, 360, 160, NULL, NULL, wc.hInstance, NULL);
	return h;
}

static void pump(HANDLE p, DWORD ms)
{
	MSG m;
	for (DWORD t0 = GetTickCount(), t; (t = GetTickCount() - t0) < ms; ) {
		DWORD r = MsgWaitForMultipleObjects(p ? 1 : 0, &p, FALSE, ms - t, QS_ALLINPUT);
		if (r != WAIT_OBJECT_0 + (p ? 1 : 0)) break;
		while (PeekMessageA(&m, NULL, 0, 0, PM_REMOVE)) DispatchMessageA(&m);
	}
}

/* takes the foreground as a program the user just used has it: this
   process sends the last input (a zero mouse move, else F24, a key no
   keyboard has); 0 none worked, else which */
static int take(HWND h)
{
	if (SetForegroundWindow(h) && GetForegroundWindow() == h) return 1;
	INPUT mv = { .type = INPUT_MOUSE, .mi = { .dwFlags = MOUSEEVENTF_MOVE } };
	SendInput(1, &mv, sizeof mv);
	if (SetForegroundWindow(h) && GetForegroundWindow() == h) return 2;
	INPUT k[2] = { { .type = INPUT_KEYBOARD, .ki = { .wVk = VK_F24 } },
	               { .type = INPUT_KEYBOARD, .ki = { .wVk = VK_F24, .dwFlags = KEYEVENTF_KEYUP } } };
	SendInput(2, k, sizeof *k);
	if (SetForegroundWindow(h) && GetForegroundWindow() == h) return 3;
	return 0;
}
static int how;

static HANDLE run(const char *args)
{
	char self[MAX_PATH], line[2 * MAX_PATH];
	STARTUPINFOA si = { .cb = sizeof si };
	PROCESS_INFORMATION pi;
	GetModuleFileNameA(NULL, self, sizeof self);
	snprintf(line, sizeof line, "\"%s\" %s", self, args);
	if (!CreateProcessA(NULL, line, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) return NULL;
	CloseHandle(pi.hThread);
	return pi.hProcess;
}

static DWORD code(HANDLE p)
{
	DWORD c = 99;
	WaitForSingleObject(p, 20000);
	GetExitCodeProcess(p, &c);
	CloseHandle(p);
	return c;
}

int main(int argc, char **argv)
{
	if (argc == 3 && !strcmp(argv[1], "launch")) return remote_open(argv[2]);
	if (argc == 2 && !strcmp(argv[1], "editor")) {
		if (remote_open(NULL)) return 90;   /* another editor runs */
		HWND h = mkwin("probe: editor", 520);
		HANDLE ready = OpenEventA(EVENT_MODIFY_STATE, FALSE, "Local\\raise-probe-ready");
		HANDLE quit = OpenEventA(SYNCHRONIZE, FALSE, "Local\\raise-probe-quit");
		SetEvent(ready);
		for (;;) {
			pump(quit, 100);
			if (WaitForSingleObject(quit, 0) == WAIT_OBJECT_0) break;
			for (char *s; (s = remote_next()); free(s)) {
				if (IsIconic(h)) ShowWindow(h, SW_RESTORE);
				BOOL ok = SetForegroundWindow(h);   /* as win_raise */
				fprintf(stderr, "  editor: SetForegroundWindow %s\n", ok ? "TRUE" : "FALSE");
			}
		}
		remote_stop();
		return 0;
	}
	if (argc == 3 && !strcmp(argv[1], "front")) {
		HWND h = mkwin("CLICK INSIDE THIS WINDOW (probe: program in front)", 100);
		pump(NULL, 200);
		how = take(h);
		if (!how) {
			printf("  click inside the window \"CLICK INSIDE THIS WINDOW\" (60 s)...\n");
			fflush(stdout);
			FlashWindow(h, TRUE);
			for (int i = 0; i < 600 && GetForegroundWindow() != h; i++) pump(NULL, 100);
			how = 4;
		}
		pump(NULL, 200);
		if (GetForegroundWindow() != h) return 50;   /* could not stand in */
		SetWindowTextA(h, "probe: program in front");
		fprintf(stderr, "  front: in front by %s\n", how == 1 ? "asking" : how == 2 ? "a zero mouse move" : how == 3 ? "F24" : "your click");
		char a[MAX_PATH + 16];
		snprintf(a, sizeof a, "launch %s", argv[2]);
		HANDLE l = run(a);
		if (!l) return 51;
		pump(l, 10000);   /* the launch only sends to the editor */
		DWORD lc = 99;
		GetExitCodeProcess(l, &lc);
		CloseHandle(l);
		pump(NULL, 600);
		HWND fg = GetForegroundWindow();
		char t[64] = "";
		GetWindowTextA(fg, t, sizeof t);
		if (lc != 1) return 52;   /* not taken */
		return fg == h ? 2 : !strcmp(t, "probe: editor") ? 1 : 3;
	}

	DWORD lock = 0;
	SystemParametersInfoA(SPI_GETFOREGROUNDLOCKTIMEOUT, 0, &lock, 0);
	LASTINPUTINFO li = { .cbSize = sizeof li };
	GetLastInputInfo(&li);
	printf("foreground lock timeout %lu ms; last input %lu ms ago\n", lock, GetTickCount() - li.dwTime);
	HWND orig = GetForegroundWindow();
	HANDLE ready = CreateEventA(NULL, TRUE, FALSE, "Local\\raise-probe-ready");
	HANDLE quit = CreateEventA(NULL, TRUE, FALSE, "Local\\raise-probe-quit");
	HANDLE ed = run("editor");
	if (WaitForSingleObject(ready, 10000) != WAIT_OBJECT_0) { printf("editor did not start (%lu)\n", code(ed)); return 1; }
	static const char *what[] = { "?", "EDITOR in front", "program in front kept it", "another window in front" };
	int n = argc > 1 ? atoi(argv[1]) : 5;
	for (int i = 0; i < n; i++) {
		char a[64];
		snprintf(a, sizeof a, "front f%d.txt", i);
		DWORD c = code(run(a));
		printf("round %d: %s\n", i, c <= 3 ? what[c] : c == 50 ? "stand-in could not take the foreground" : c == 52 ? "file not taken" : "error");
		fflush(stdout);
	}
	SetEvent(quit);
	code(ed);
	/* the window in front before the probe gets it back */
	if (orig) printf("restored the window in front before: %s\n", take(orig) ? "yes" : "no");
	return 0;
}
