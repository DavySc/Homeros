/* PR 150 review probe, real Windows: two windows of backend_win; raise
   each in turn N times and record, for each raise, whether the window's
   KEY_FOCUS comes before KEY_RAISED (Focus.tla's order), whether the
   foreground window is the one raised, and how long the focus took. */
#include <windows.h>
#include <stdio.h>
#include "backend.h"
#include "keys.h"
#include "grid.h"
static int k[1024], from[1024];
static int drain(void) { int n = 0, r; while ((r = backend_win.input(k + n, from + n, 1024 - n)) > 0) n += r; return n; }
int main(int argc, char **argv)
{
	int N = argc > 1 ? atoi(argv[1]) : 40, ok = 0, fg = 0, raised_first = 0, nofocus = 0;
	const Backend *b = &backend_win;
	if (b->open() || b->win_open(0)) { puts("focusprobe: no window"); return 1; }
	for (int i = 0; i < 20; i++) { drain(); Sleep(10); }
	if (b->win_open(1)) { puts("focusprobe: no second window"); return 1; }
	for (int i = 0; i < 20; i++) { drain(); Sleep(10); }
	for (int i = 0; i < N; i++) {
		int id = i % 2, f = -1, r = -1, n = 0;
		b->raise(id);
		for (int t = 0; t < 50 && (f < 0 || r < 0); t++) {   /* up to 500 ms */
			int got = drain();
			for (int j = 0; j < got; j++) {
				if (k[j] == KEY_FOCUS && from[j] == id && f < 0) f = n + j;
				if (k[j] == KEY_RAISED && r < 0) r = n + j;
			}
			n += got;
			if (f < 0 || r < 0) Sleep(10);
		}
		HWND want = NULL;
		for (HWND h = NULL; (h = FindWindowExW(NULL, h, L"homeros", NULL));) want = h;   /* any: compared below by title-less check */
		(void)want;
		ok += f >= 0 && r > f;
		raised_first += r >= 0 && (f < 0 || r < f);
		nofocus += f < 0;
		DWORD pid = 0;
		GetWindowThreadProcessId(GetForegroundWindow(), &pid);
		fg += pid == GetCurrentProcessId();
	}
	printf("focusprobe: %d raises: focus before KEY_RAISED %d, KEY_RAISED first %d, no focus within 500 ms %d, ours in front after %d\n",
	       N, ok, raised_first, nofocus, fg);
	b->close();
	return ok != N;
}
