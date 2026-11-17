/* Finding 3: keys can stay undelivered while the loop sleeps.  The loop
   (loop.h: LOOP_MSGQ, MsgWaitForMultipleObjectsEx with QS_ALLINPUT) calls
   backend_input once per wake, as src/main.c's on_input does with 256
   slots.  backend_input stops pumping when its ring is full, and the
   messages it leaves in the queue are no longer new input, so the next wait
   does not return for them unless it is given MWMO_INPUTAVAILABLE. */
#include <windows.h>
#include <stdio.h>
#include "backend.h"

static const char *waitq(DWORD flags)
{
	return MsgWaitForMultipleObjectsEx(0, NULL, 300, QS_ALLINPUT, flags) == WAIT_TIMEOUT
	       ? "times out (the loop would sleep)" : "returns (the loop would read)";
}

int main(void)
{
	static int k[256];
	int bad = 0;
	if (backend_win.open()) return 2;
	HWND win = FindWindowW(L"homeros", NULL);
	while (backend_win.input(k, 256) > 0) {}

	/* a 300-character paste, sent by a tool as WM_CHAR */
	for (int i = 0; i < 300; i++) PostMessageW(win, WM_CHAR, 'x', 0);
	int n = backend_win.input(k, 256);                /* one wake, one call */
	const char *w = waitq(0);
	printf("300 keys posted, one backend_win.input(256) gives %d; the next wait %s\n", n, w);
	bad += w[0] == 't';
	printf("with MWMO_INPUTAVAILABLE the wait %s\n", waitq(MWMO_INPUTAVAILABLE));
	while (backend_win.input(k, 256) > 0) {}

	backend_win.close();
	return bad ? 1 : 0;
}
