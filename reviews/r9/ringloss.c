/* Note 7: put() drops a key without a trace when the 256-key ring is full.
   backend_input stops pumping at a full ring, so this needs the window
   procedure to be entered from elsewhere: here, WM_CHAR sent instead of
   posted.  An IME commit is not such a path under Wine 11.0 (DefWindowProc
   posts the WM_CHAR of a WM_IME_CHAR: 270 of 270 arrive); on Windows that is
   untested. */
#include <windows.h>
#include <stdio.h>
#include "backend.h"

static int drain(void)
{
	static int k[64];
	int got = 0, r;
	while ((r = backend_input(k, 64)) > 0) got += r;
	return got;
}

int main(void)
{
	if (backend_open()) return 2;
	HWND win = FindWindowW(L"homeros", NULL);
	drain();
	for (int i = 0; i < 300; i++) SendMessageW(win, WM_CHAR, 'x', 0);
	int a = drain();
	printf("300 WM_CHAR sent:                          %d keys arrive\n", a);
	for (int i = 0; i < 250; i++) PostMessageW(win, WM_CHAR, 'x', 0);
	int k, got = backend_input(&k, 1);
	for (int i = 0; i < 20; i++) SendMessageW(win, WM_IME_CHAR, 0x3042, 0);
	got += drain();
	printf("250 typed, then a 20-character IME commit: %d of 270 keys arrive\n", got);
	backend_close();
	return a != 300;
}
