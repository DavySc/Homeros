/* The Win32 backend, black box through its Backend table and the window it
   opens.  Keys: surrogate pairs joined and a lone low one dropped, Backspace
   as 127 and Ctrl-H as 8, the four arrows, none lost when the caller takes
   fewer than are waiting; the mouse and the wheel at their cells; the
   clipboard both ways with CRLF.  Drawing: only changed cells reach the
   window; a letter in the text, dim and face colours; a wide character over
   two cells; the cursor reversed, restored when it leaves, and drawn at
   (0, 0) after a whole redraw; WM_PAINT repaints from the DIB; a new size or
   DPI comes as KEY_RESIZE and is redrawn whole; the close button gives KEY_CLOSE
   and leaves the window.  Pacing: no frame sooner than P + P/8 after the last (Pace.tla),
   the timer wakes the loop when the next may be drawn and stays quiet when
   idle; nothing is drawn while minimised.
   Not tested here, with the reason: an astral character's glyph (Wine has
   no font with one, so right and wrong surrogates both draw a box), and
   WM_DISPLAYCHANGE (Wine's DWM period never changes).
   Runs on Windows or under Wine (make win); elsewhere it says so. */
#ifdef _WIN32
#include <windows.h>
#include <dwmapi.h>
#include <string.h>
#include "check.h"
#include "backend.h"
#include "keys.h"
#include "loop.h"

static const Backend *b = &backend_win;
static HWND win;
static int cw, ch;
static const COLORREF BG = RGB(30, 30, 46), REV = RGB(205, 214, 244), RED = RGB(255, 0, 0);

static int input(int *k, int max)
{
	int got = 0, r;
	while ((r = b->input(k + got, max)) > 0) got += r;
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

/* how many pixels of cell (r, c) have colour p */
static int count(int r, int c, COLORREF p)
{
	HDC dc = GetDC(win);
	int n = 0;
	for (int y = r * ch; y < (r + 1) * ch; y++)
		for (int x = c * cw; x < (c + 1) * cw; x++) n += GetPixel(dc, x, y) == p;
	ReleaseDC(win, dc);
	return n;
}

/* how many pixels of cell (r, c) are not the background */
static int inked(int r, int c) { return cw * ch - count(r, c, BG); }

static int lum(COLORREF p) { return GetRValue(p) + GetGValue(p) + GetBValue(p); }

/* the pixel of cell (r, c) furthest from the background */
static COLORREF ink(int r, int c)
{
	HDC dc = GetDC(win);
	COLORREF best = BG;
	int most = -1;
	for (int y = r * ch; y < (r + 1) * ch; y++)
		for (int x = c * cw; x < (c + 1) * cw; x++) {
			COLORREF p = GetPixel(dc, x, y);
			int d = abs(GetRValue(p) - GetRValue(BG)) + abs(GetGValue(p) - GetGValue(BG)) + abs(GetBValue(p) - GetBValue(BG));
			if (d > most) { most = d; best = p; }
		}
	ReleaseDC(win, dc);
	return best;
}

static void paint(COLORREF p)
{
	RECT rc;
	HDC dc = GetDC(win);
	HBRUSH br = CreateSolidBrush(p);
	GetClientRect(win, &rc);
	FillRect(dc, &rc, br);
	DeleteObject(br);
	ReleaseDC(win, dc);
}

/* waits until the backend allows a frame, as the loop would */
static void wait_ready(void)
{
	int fds[2];
	CHECK(b->fds(fds) == 2);
	int wakes = 0;   /* one, or two if the timer's clock ran a little ahead */
	for (; wakes < 3 && !b->ready(); wakes++)
		CHECK(WaitForSingleObject((HANDLE)(INT_PTR)fds[1], 1000) == WAIT_OBJECT_0);
	CHECK(wakes <= 2 && b->ready());
}

static void present(const Grid *g, Grid *shown)
{
	wait_ready();
	b->present(g, shown);
}

static void put(Grid *g, int r, int c, const char *s, int width, int attr)
{
	grid_put(g, r, c, (const unsigned char *)s, strlen(s), width, attr);
}

/* a UBSan trap is an illegal instruction: report it and exit, where Wine
   would otherwise start its debugger and wait */
static LONG WINAPI crashed(EXCEPTION_POINTERS *e)
{
	fprintf(stderr, "test_win: FAIL, exception 0x%lx\n", (unsigned long)e->ExceptionRecord->ExceptionCode);
	ExitProcess(3);
}

int main(void)
{
	static int k[1024];
	int rows, cols, fds[2], r, c;
	Grid g = { 0 }, shown = { 0 };

	SetUnhandledExceptionFilter(crashed);
	CHECK(!b->open());
	win = FindWindowW(L"homeros", NULL);
	CHECK(win);
	CHECK(b->fds(fds) == 2 && fds[0] == LOOP_MSGQ && fds[1] != 0);
	input(k, 1024);   /* whatever showing the window sent */

	/* keys */
	static const WPARAM chars[] = { 'a', 0xD835, 0xDCDD, 8, 27, 0xDCDD, 0xE9, 13 };
	for (unsigned i = 0; i < sizeof chars / sizeof *chars; i++) PostMessageW(win, WM_CHAR, chars[i], 0);
	static const WPARAM vks[] = { VK_LEFT, VK_RIGHT, VK_UP, VK_DOWN, VK_DELETE };
	for (unsigned i = 0; i < 5; i++) PostMessageW(win, WM_KEYDOWN, vks[i], 0);
	CHECK(input(k, 1) == 11);   /* one slot per call: nothing lost; the lone 0xDCDD gives none */
	CHECK(k[0] == 'a' && k[1] == 0x1D4DD && k[2] == KEY_BS && k[3] == KEY_ESC && k[4] == 0xE9
	      && k[5] == KEY_ENTER && k[6] == KEY_LEFT && k[7] == KEY_RIGHT && k[8] == KEY_UP && k[9] == KEY_DOWN && k[10] == KEY_DEL);
	BYTE ks[256] = { 0 };
	ks[VK_CONTROL] = 0x80;
	SetKeyboardState(ks);
	PostMessageW(win, WM_CHAR, 8, 0);
	CHECK(input(k, 4) == 1 && k[0] == 8);   /* Ctrl-H */
	ks[VK_CONTROL] = 0;
	SetKeyboardState(ks);
	for (int i = 0; i < 600; i++) PostMessageW(win, WM_CHAR, 'x', 0);
	CHECK(input(k, 7) == 600);

	/* cell size, measured: a reversed space at (0, 0) */
	b->size(&rows, &cols);
	CHECK(rows > 9 && cols > 10);
	CHECK(!grid_resize(&g, rows, cols));
	g.crow = 2; g.ccol = 2;
	put(&g, 0, 0, " ", 1, ATTR_REV);
	present(&g, &shown);
	while (px(cw, 0) == REV) cw++;
	while (px(0, ch) == REV) ch++;
	CHECK(cw > 3 && ch > 3 && px(cw, 0) == BG && px(0, ch) == BG);
	CHECK(at(2, 2) == REV && at(1, 1) == BG);   /* the cursor */

	/* the mouse and the wheel, at their cells */
	PostMessageW(win, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(3 * cw + 1, 2 * ch + 1));
	CHECK(input(k, 4) == 1 && k[0] == KEY_MOUSE_LEFT);
	b->mouse(&r, &c);
	CHECK(r == 2 && c == 3);
	POINT pt = { 4 * cw + 1, 1 * ch + 1 };
	ClientToScreen(win, &pt);
	PostMessageW(win, WM_MOUSEWHEEL, MAKEWPARAM(0, 2 * WHEEL_DELTA), MAKELPARAM(pt.x, pt.y));
	PostMessageW(win, WM_MOUSEWHEEL, MAKEWPARAM(0, -WHEEL_DELTA / 2), MAKELPARAM(pt.x, pt.y));
	PostMessageW(win, WM_MOUSEWHEEL, MAKEWPARAM(0, -WHEEL_DELTA / 2), MAKELPARAM(pt.x, pt.y));
	CHECK(input(k, 8) == 3 && k[0] == KEY_WHEEL_UP && k[1] == KEY_WHEEL_UP && k[2] == KEY_WHEEL_DOWN);
	b->mouse(&r, &c);
	CHECK(r == 1 && c == 4);

	/* the clipboard: UTF-8 with LF in, UTF-16 with CRLF on the clipboard */
	static const char clip[] = "a\n\xc3\xa9\xf0\x9d\x93\x9d";   /* a, LF, é, 𝓝 */
	char buf[16];
	CHECK(!b->clip_set(clip, sizeof clip - 1));
	CHECK(OpenClipboard(NULL));
	const WCHAR *u = GlobalLock(GetClipboardData(CF_UNICODETEXT));
	CHECK(u && !wcscmp(u, L"a\r\n\xe9\xd835\xdcdd"));
	GlobalUnlock(GetClipboardData(CF_UNICODETEXT));
	CloseClipboard();
	CHECK(b->clip_get(buf, sizeof buf) == (long)sizeof clip - 1 && !memcmp(buf, clip, sizeof clip - 1));
	memset(buf, 0, sizeof buf);
	CHECK(b->clip_get(buf, 3) == (long)sizeof clip - 1 && !memcmp(buf, clip, 3) && !buf[3]);

	/* the same frame again draws nothing */
	paint(RED);
	present(&g, &shown);
	CHECK(at(0, 0) == RED && at(2, 2) == RED && at(rows - 1, cols - 1) == RED);

	/* one change draws that cell; the cursor moves: both of its cells */
	put(&g, 3, 5, " ", 1, ATTR_REV);
	g.crow = 1; g.ccol = 1;
	present(&g, &shown);
	CHECK(at(3, 5) == REV && at(1, 1) == REV && at(2, 2) == BG);
	CHECK(at(0, 0) == RED && at(3, 4) == RED && at(3, 6) == RED && at(rows - 1, cols - 1) == RED);

	/* text: a letter, a dim one, a string-face one, a wide reversed one */
	paint(BG);
	put(&g, 4, 1, "M", 1, ATTR_NONE);
	put(&g, 5, 1, "M", 1, ATTR_DIM);
	put(&g, 6, 1, "M", 1, ATTR_NONE);
	g.c[6 * g.cols + 1].face = FACE_STRING;
	put(&g, 7, 1, "\xe4\xb8\xad", 2, ATTR_REV);   /* U+4E2D, two cells */
	put(&g, 8, 1, " ", 1, ATTR_NONE);
	g.c[8 * g.cols + 1].face = FACE_COMMENT_TODO;   /* a face with a background */
	present(&g, &shown);
	/* glyph edges are blended (ClearType), so few pixels are exactly the
	   text colour: look at the inked pixels and the one furthest from the
	   background instead */
	COLORREF m = ink(4, 1), d = ink(5, 1), s = ink(6, 1);
	CHECK(inked(4, 1) > 0 && inked(4, 1) < cw * ch);   /* a letter, not a filled cell */
	CHECK(inked(5, 1) > 0 && lum(d) < lum(m));         /* dim is darker */
	CHECK(inked(6, 1) > 0 && GetGValue(s) > GetRValue(s) && GetGValue(s) > GetBValue(s));   /* green */
	CHECK(GetBValue(m) > GetGValue(m));                /* the text colour is blue-white, not green */
	CHECK(count(7, 2, REV) > cw * ch / 2 && count(7, 3, REV) == 0);
	CHECK(count(8, 1, RGB(faces[FACE_COMMENT_TODO].bg[0], faces[FACE_COMMENT_TODO].bg[1], faces[FACE_COMMENT_TODO].bg[2])) == cw * ch);

	/* covered and uncovered: WM_PAINT draws from the DIB */
	paint(RED);
	InvalidateRect(win, NULL, FALSE);
	UpdateWindow(win);
	CHECK(at(5, 5) == BG && at(3, 5) == REV && count(4, 1, RED) == 0);

	/* the close button is a key; the window stays until the editor quits */
	PostMessageW(win, WM_CLOSE, 0, 0);
	CHECK(input(k, 4) == 1 && k[0] == KEY_CLOSE && IsWindow(win));

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
	b->present(&g, &shown);
	CHECK(!b->ready());
	do QueryPerformanceCounter(&t1); while (!b->ready());
	CHECK(p > 0 && t1.QuadPart - t0.QuadPart >= p + p / 8);
	printf("test_win: next frame allowed after %.2f ms, refresh period %.2f ms\n",
	       (double)(t1.QuadPart - t0.QuadPart) * 1000 / (double)f.QuadPart, (double)p * 1000 / (double)f.QuadPart);
	present(&g, &shown);
	/* idle: after a frame with nothing left to draw the timer stays quiet */
	present(&g, &shown);
	CHECK(WaitForSingleObject((HANDLE)(INT_PTR)fds[1], 200) == WAIT_TIMEOUT);

	/* minimised: never ready; Ctrl-Z minimises (gvim) and main.c then
	   redraws whole: the cursor at (0, 0) is drawn too */
	b->suspend();
	CHECK(IsIconic(win) && !b->ready());
	ShowWindow(win, SW_RESTORE);
	input(k, 1024);
	grid_free(&shown);
	g.crow = 0; g.ccol = 3;
	paint(RED);
	present(&g, &shown);
	CHECK(at(0, 3) == REV && at(0, 4) == BG);
	g.crow = 0; g.ccol = 0;
	grid_clear(&g);
	grid_free(&shown);
	paint(RED);
	present(&g, &shown);
	CHECK(at(0, 0) == REV && at(0, 1) == BG);

	/* a new size: KEY_RESIZE, then the whole window is drawn */
	RECT wr;
	GetWindowRect(win, &wr);
	SetWindowPos(win, NULL, 0, 0, wr.right - wr.left + 5 * cw, wr.bottom - wr.top + 3 * ch, SWP_NOMOVE | SWP_NOZORDER);
	int n = input(k, 1024), r2, c2, resized = 0;
	for (int i = 0; i < n; i++) resized |= k[i] == KEY_RESIZE;
	CHECK(resized);
	b->size(&r2, &c2);
	CHECK(r2 >= rows + 2 && c2 >= cols + 4);
	CHECK(!grid_resize(&g, r2, c2));   /* puts the cursor at (0, 0) */
	paint(RED);
	present(&g, &shown);
	CHECK(at(0, 0) == REV && at(0, 1) == BG && at(r2 - 1, c2 - 1) == BG && shown.rows == r2 && shown.cols == c2);

	/* a new DPI, twice the old, in a window of the same size: the cells
	   double, so about half the rows and columns, and all is redrawn */
	GetWindowRect(win, &wr);
	SendMessageW(win, WM_DPICHANGED, MAKEWPARAM(2 * GetDpiForWindow(win), 2 * GetDpiForWindow(win)), (LPARAM)&wr);
	n = input(k, 1024);
	resized = 0;
	for (int i = 0; i < n; i++) resized |= k[i] == KEY_RESIZE;
	CHECK(resized);
	b->size(&rows, &cols);
	CHECK(rows < r2 * 6 / 10 && cols < c2 * 6 / 10);   /* font sizes round, so not exactly half */
	CHECK(!grid_resize(&g, rows, cols));
	put(&g, 0, 0, " ", 1, ATTR_REV);
	g.crow = rows - 1;
	paint(RED);
	present(&g, &shown);
	int w2 = 0;
	while (px(w2, 0) == REV) w2++;
	CHECK(w2 >= 2 * cw - 2 && w2 <= 2 * cw + 2 && px(w2, 0) == BG && px(c2 * cw - 1, r2 * ch - 1) != RED);

	b->close();
	CHECK(!FindWindowW(L"homeros", NULL));
	grid_free(&g);
	grid_free(&shown);
	puts("test_win: keys, mouse and clipboard; identical frame draws nothing; one change draws one cell; "
	     "text, dim, face, face background and wide cells; cursor drawn, restored and at (0,0) after a whole redraw; WM_PAINT; "
	     "resize and DPI redraw whole; close is a key; paced; idle quiet; minimised draws nothing");
	return 0;
}
#else
#include <stdio.h>
int main(void) { puts("test_win: Windows only (make win runs it under Wine)"); return 0; }
#endif
