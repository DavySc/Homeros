/* PR 156 review probe: a WM_COPYDATA sent but not yet read when the editor
   destroys its window (remote_stop after a :q!).  Remote.tla's NoLoss needs
   the sender to be told "not taken" and the window's procedure never to
   run for it.  The editor stand-in never reads messages before the
   destroy; the sender (this program with "send") is blocked in SendMessage
   by then.  Prints, per round, whether the procedure ran and what the
   sender was told.  argv[1]: rounds. */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int got;
static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
	if (m == WM_COPYDATA) { got++; return 1; }
	return DefWindowProcW(h, m, wp, lp);
}
int main(int argc, char **argv)
{
	if (argc == 2 && !strcmp(argv[1], "send")) {
		HWND h = FindWindowExW(HWND_MESSAGE, NULL, L"probe-inflight", NULL);
		if (!h) return 50;
		COPYDATASTRUCT cd = { 1, 2, (void *)"x" };
		SetEvent(OpenEventW(EVENT_MODIFY_STATE, FALSE, L"Local\\probe-inflight-sending"));
		return 10 + (int)SendMessageW(h, WM_COPYDATA, 0, (LPARAM)&cd);
	}
	int rounds = argc > 1 ? atoi(argv[1]) : 20, ran = 0, told = 0;
	WNDCLASSW wc = { .lpfnWndProc = proc, .hInstance = GetModuleHandleW(NULL), .lpszClassName = L"probe-inflight" };
	RegisterClassW(&wc);
	HANDLE ev = CreateEventW(NULL, FALSE, FALSE, L"Local\\probe-inflight-sending");
	for (int i = 0; i < rounds; i++) {
		HWND h = CreateWindowExW(0, L"probe-inflight", NULL, 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, wc.hInstance, NULL);
		char self[MAX_PATH], line[MAX_PATH + 16];
		STARTUPINFOA si = { .cb = sizeof si };
		PROCESS_INFORMATION pi;
		GetModuleFileNameA(NULL, self, sizeof self);
		snprintf(line, sizeof line, "\"%s\" send", self);
		got = 0;
		if (!h || !CreateProcessA(NULL, line, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) { puts("inflight: cannot start"); return 2; }
		WaitForSingleObject(ev, 10000);
		Sleep(200);                      /* the sender is inside SendMessage now */
		int before = got;
		DestroyWindow(h);
		int at = got;
		MSG m;
		DWORD code = 0;
		while (MsgWaitForMultipleObjects(1, &pi.hProcess, FALSE, 10000, QS_ALLINPUT) == WAIT_OBJECT_0 + 1)
			while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) DispatchMessageW(&m);
		GetExitCodeProcess(pi.hProcess, &code);
		CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
		if (before || at || got) ran++;
		if (code != 10) told++;
		if (i < 3 || before || at || got || code != 10) printf("round %d: procedure ran before the destroy %d, inside it %d, after %d; sender's exit code %lu (10: not taken, 11: taken)\n", i, before, at - before, got - at, (unsigned long)code);
	}
	printf("inflight: %d rounds: the procedure ran for the unread message in %d, the sender was told other than \"not taken\" in %d\n", rounds, ran, told);
	return ran || told;
}
