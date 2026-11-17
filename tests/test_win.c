/* The Win32 backend, black box through backend.h and the window it opens:
   keys from window messages (surrogate pairs joined, Backspace as 127,
   arrows, none lost when the caller takes fewer than are waiting); only
   changed cells reach the window (paint over it, present the same frame,
   the paint stays); the cursor cell is drawn reversed and restored when the
   cursor leaves; a new size comes as KEY_RESIZE and is redrawn whole; no
   frame sooner than P + P/8 after the last (Pace.tla), the pacing timer
   wakes the loop when the next may be drawn and stays quiet when idle;
   nothing is drawn while minimised.
   Runs on Windows or under Wine (src/win/check.sh); elsewhere it says so. */
#ifdef _WIN32
#include <windows.h>
#include <dwmapi.h>
#include "check.h"
#include "backend.h"
#include "keys.h"

static HWND win;
static int cw, ch;
static const COLORREF BG = RGB(30, 30, 46), REV = RGB(205, 214, 244), RED = RGB(255, 0, 0);

static int input(int *k, int max)
{
	int got = 0, r;
	while ((r = backend_input(k + got, max)) > 0) got += r;
	return got;
}

static COLORREF px(int x, int y)
{
	HDC dc = GetDC(win);
	COLORREF p = GetPixel(dc, x, y);
	ReleaseDC(win, dc);
	return p;
}

static COLORREF at(int r, int c) { return px(c * cw + cw / 2, r * ch + ch / 2); }

static void paint_red(void)
{
	RECT rc;
	HDC dc = GetDC(win);
	HBRUSH b = CreateSolidBrush(RED);
	GetClientRect(win, &rc);
	FillRect(dc, &rc, b);
	DeleteObject(b);
	ReleaseDC(win, dc);
}

/* waits until the backend allows a frame, as the loop would */
static void wait_ready(void)
{
	int fds[2];
	CHECK(backend_fds(fds) == 2);
	int wakes = 0;   /* one, or two if the timer's clock ran a little ahead */
	for (; wakes < 3 && !backend_ready(); wakes++)
		CHECK(WaitForSingleObject((HANDLE)(INT_PTR)fds[1], 1000) == WAIT_OBJECT_0);
	CHECK(wakes <= 2 && backend_ready());
}

int main(void)
{
	static int k[1024];
	int rows, cols, fds[2];
	Grid g = { 0 }, shown = { 0 };

	CHECK(!backend_open());
	win = FindWindowW(L"homeros", NULL);
	CHECK(win);
	CHECK(backend_fds(fds) == 2 && fds[0] == -1 && fds[1] != 0);
	input(k, 1024);   /* whatever showing the window sent */

	/* keys */
	static const WPARAM chars[] = { 'a', 0xD835, 0xDCDD, 8, 27, 0xE9, 13 };
	for (unsigned i = 0; i < sizeof chars / sizeof *chars; i++) PostMessageW(win, WM_CHAR, chars[i], 0);
	PostMessageW(win, WM_KEYDOWN, VK_LEFT, 0);
	PostMessageW(win, WM_KEYDOWN, VK_UP, 0);
	CHECK(input(k, 1) == 8);   /* one slot per call: nothing lost */
	CHECK(k[0] == 'a' && k[1] == 0x1D4DD && k[2] == KEY_BS && k[3] == KEY_ESC && k[4] == 0xE9
	      && k[5] == KEY_ENTER && k[6] == KEY_LEFT && k[7] == KEY_UP);
	for (int i = 0; i < 600; i++) PostMessageW(win, WM_CHAR, 'x', 0);
	CHECK(input(k, 7) == 600);

	/* cell size, measured: a reversed space at (0, 0) */
	backend_size(&rows, &cols);
	CHECK(rows > 3 && cols > 10);
	CHECK(!grid_resize(&g, rows, cols));
	g.crow = 2; g.ccol = 2;
	grid_put(&g, 0, 0, (const unsigned char *)" ", 1, 1, ATTR_REV);
	wait_ready();
	backend_present(&g, &shown);
	while (px(cw, 0) == REV) cw++;
	while (px(0, ch) == REV) ch++;
	CHECK(cw > 3 && ch > 3 && px(cw, 0) == BG && px(0, ch) == BG);
	CHECK(at(2, 2) == REV && at(1, 1) == BG);   /* the cursor */

	/* the same frame again draws nothing */
	paint_red();
	wait_ready();
	backend_present(&g, &shown);
	CHECK(at(0, 0) == RED && at(2, 2) == RED && at(rows - 1, cols - 1) == RED);

	/* one change draws that cell; the cursor moves: both of its cells */
	grid_put(&g, 3, 5, (const unsigned char *)" ", 1, 1, ATTR_REV);
	g.crow = 1; g.ccol = 1;
	wait_ready();
	backend_present(&g, &shown);
	CHECK(at(3, 5) == REV && at(1, 1) == REV && at(2, 2) == BG);
	CHECK(at(0, 0) == RED && at(3, 4) == RED && at(3, 6) == RED && at(rows - 1, cols - 1) == RED);

	/* pacing (Pace.tla): the next frame no sooner than P + P/8 after the
	   last, found by asking until it is allowed; then the timer wakes the
	   loop for the one after (wait_ready) */
	LARGE_INTEGER f, t0, t1;
	DWM_TIMING_INFO ti = { .cbSize = sizeof ti };
	CHECK(SUCCEEDED(DwmGetCompositionTimingInfo(NULL, &ti)));
	LONGLONG p = (LONGLONG)ti.qpcRefreshPeriod;
	QueryPerformanceFrequency(&f);
	wait_ready();
	QueryPerformanceCounter(&t0);
	backend_present(&g, &shown);
	CHECK(!backend_ready());
	do QueryPerformanceCounter(&t1); while (!backend_ready());
	CHECK(p > 0 && t1.QuadPart - t0.QuadPart >= p + p / 8);
	printf("test_win: next frame allowed after %.2f ms, refresh period %.2f ms\n",
	       (double)(t1.QuadPart - t0.QuadPart) * 1000 / (double)f.QuadPart, (double)p * 1000 / (double)f.QuadPart);
	backend_present(&g, &shown);
	wait_ready();
	/* idle: after a frame with nothing left to draw the timer stays quiet */
	backend_present(&g, &shown);
	CHECK(WaitForSingleObject((HANDLE)(INT_PTR)fds[1], 200) == WAIT_TIMEOUT);

	/* minimised: never ready; Ctrl-Z minimises (gvim) */
	backend_suspend();
	CHECK(IsIconic(win) && !backend_ready());
	ShowWindow(win, SW_RESTORE);
	input(k, 1024);
	wait_ready();

	/* a new size: KEY_RESIZE, then the whole window is drawn */
	RECT wr;
	GetWindowRect(win, &wr);
	SetWindowPos(win, NULL, 0, 0, wr.right - wr.left + 5 * cw, wr.bottom - wr.top + 3 * ch, SWP_NOMOVE | SWP_NOZORDER);
	int n = input(k, 1024), r2, c2, resized = 0;
	for (int i = 0; i < n; i++) resized |= k[i] == KEY_RESIZE;
	CHECK(resized);
	backend_size(&r2, &c2);
	CHECK(r2 >= rows + 2 && c2 >= cols + 4);
	CHECK(!grid_resize(&g, r2, c2));
	paint_red();
	wait_ready();
	backend_present(&g, &shown);
	CHECK(at(0, 0) == BG && at(r2 - 1, c2 - 1) == BG && shown.rows == r2 && shown.cols == c2);

	backend_close();
	CHECK(!FindWindowW(L"homeros", NULL));
	grid_free(&g);
	grid_free(&shown);
	puts("test_win: keys from messages, none lost; identical frame draws nothing; one change draws one cell; "
	     "cursor drawn and restored; resize redraws whole; paced; minimised draws nothing");
	return 0;
}
#else
#include <stdio.h>
int main(void) { puts("test_win: Windows only (src/win/check.sh runs it under Wine)"); return 0; }
#endif
