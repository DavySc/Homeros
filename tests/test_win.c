/* The Win32 backend, black box through its Backend table and the window it
   opens.  Keys: surrogate pairs joined and a lone low one dropped, Backspace
   as 127 and Ctrl-H as 8, the four arrows, none lost when the caller takes
   fewer than are waiting; the mouse and the wheel at their cells; the
   clipboard both ways with CRLF.  Drawing: only changed rows reach the
   window; a letter in the text, dim and face colours; a wide character over
   two cells; the cursor reversed, restored when it leaves, and drawn at
   (0, 0) after a whole redraw; WM_PAINT repaints from the DIB; a new size or
   DPI comes as KEY_RESIZE and is redrawn whole; the close button gives KEY_CLOSE
   and leaves the window.  Pacing: no frame sooner than P + P/8 after the last (Pace.tla),
   the timer wakes the loop when the next may be drawn and stays quiet when
   idle; nothing is drawn while minimised.
   Not tested here, with the reason: an astral character's glyph (Wine has
   no font with one, so right and wrong surrogates both draw a box), and
   WM_DISPLAYCHANGE (Wine's DWM period never changes), and a window's period
   on a second monitor of another rate (Wine shows one monitor): a gate on
   Davy's laptop.
   Runs on Windows or under Wine (make win); elsewhere it says so. */
#ifdef _WIN32
#include <windows.h>
#include <dwmapi.h>
#include <string.h>
#include "check.h"
#include "backend.h"
#include "font.h"
#include "image.h"
#include "keys.h"
#include "loop.h"

static const Backend *b = &backend_win;
static HWND win;
static int cw, ch;
static const COLORREF BG = RGB(30, 30, 46), REV = RGB(205, 214, 244), RED = RGB(255, 0, 0);

static int from[1024];   /* the window each key came from */
static int input(int *k, int max)
{
	int got = 0, r;
	while ((r = b->input(k + got, from, max)) > 0) got += r;
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
	for (; wakes < 3 && !b->ready(0); wakes++)
		CHECK(WaitForSingleObject((HANDLE)(INT_PTR)fds[1], 1000) == WAIT_OBJECT_0);
	CHECK(wakes <= 2 && b->ready(0));
}

static void present(const Grid *g, Grid *shown)
{
	wait_ready();
	b->present(0, g, shown);
}

static void put(Grid *g, int r, int c, const char *s, int width, int attr)
{
	grid_put(g, r, c, (const unsigned char *)s, strlen(s), width, attr);
}

/* a UBSan trap is an illegal instruction: report it and exit, where Wine
   would otherwise start its debugger and wait */
static LONG WINAPI crashed(EXCEPTION_POINTERS *e)
{
	fprintf(stderr, "test_win FAIL: exception 0x%lx\n", (unsigned long)e->ExceptionRecord->ExceptionCode);
	ExitProcess(3);
}

int main(void)
{
	static int k[1024];
	int rows, cols, fds[2], r, c;
	Grid g = { 0 }, shown = { 0 };

	SetUnhandledExceptionFilter(crashed);
	CHECK(!b->open() && !b->win_open(0));
	win = FindWindowW(L"homeros", NULL);
	CHECK(win && IsZoomed(win));   /* opens maximised */
	ShowWindow(win, SW_RESTORE);   /* the tests below size it themselves */
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
	PostMessageW(win, WM_KEYDOWN, VK_OEM_PERIOD, 0);
	CHECK(input(k, 4) == 2 && k[0] == 8 && k[1] == KEY_CTRL_DOT);   /* Ctrl-H, Ctrl+. */
	ks[VK_CONTROL] = 0;
	PostMessageW(win, WM_CHAR, 9, 0);
	CHECK(input(k, 4) == 1 && k[0] == KEY_TAB);
	ks[VK_SHIFT] = 0x80;
	SetKeyboardState(ks);
	PostMessageW(win, WM_CHAR, 9, 0);
	CHECK(input(k, 4) == 1 && k[0] == KEY_S_TAB);
	ks[VK_SHIFT] = 0;
	SetKeyboardState(ks);
	PostMessageW(win, WM_KEYDOWN, VK_OEM_PERIOD, 0);   /* a plain '.' is WM_CHAR's */
	CHECK(input(k, 4) == 1 && k[0] == '.');
	for (int i = 0; i < 600; i++) PostMessageW(win, WM_CHAR, 'x', 0);
	CHECK(input(k, 7) == 600);

	/* cell size, measured: a reversed space at (0, 0) */
	b->size(0, &rows, &cols);
	CHECK(rows > 9 && cols > 10);
	CHECK(!grid_resize(&g, rows, cols));
	g.crow = 2; g.ccol = 2;
	put(&g, 0, 0, " ", 1, ATTR_REV);
	present(&g, &shown);
	while (px(cw, 0) == REV) cw++;
	while (px(0, ch) == REV) ch++;
	CHECK(cw > 3 && ch > 3 && px(cw, 0) == BG && px(0, ch) == BG);
	CHECK(at(2, 2) == REV && at(1, 1) == BG);   /* the cursor */
	{   /* the cell is font.h's, at 12 points for the window's DPI (the
	       face itself: test_win_font) */
		int w, h, base;
		font_cell(&w, &h, &base);
		CHECK(cw == w && ch == h);
	}
	{   /* an image (package 21, thread 0's, agreed): Backend.cell is the cell
	       measured above; tests/test_image.png (32 x 16, red then blue) shows
	       from row 3, each tile its own columns, the background past it;
	       then text again */
		int w, h, n;
		b->cell(0, &w, &h);
		CHECK(w == cw && h == ch && 4 * cw > 32 && ch > 16);
		CHECK((n = image_load("tests/test_image.png", 4, 1, cw, ch, 0x1e1e2e)) > 0);
		image_put(&g, 3, 0, n, 0);
		present(&g, &shown);
		for (int y = 3 * ch; y < 3 * ch + 16; y++)
			for (int x = 0; x < 32; x++) CHECK(px(x, y) == (x < 16 ? RGB(255, 0, 0) : RGB(0, 0, 255)));
		CHECK(px(32, 3 * ch) == BG && px(0, 3 * ch + 16) == BG);
		for (int c = 0; c < 4; c++) put(&g, 3, c, " ", 1, ATTR_NONE);
		present(&g, &shown);
		CHECK(px(1, 3 * ch + 1) == BG && px(20, 3 * ch + 1) == BG);
	}

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

	/* one change draws its row (the painter's damage is rows, as
	   Wayland's); the cursor moves: both of its rows; no other row */
	put(&g, 3, 5, " ", 1, ATTR_REV);
	g.crow = 1; g.ccol = 1;
	present(&g, &shown);
	CHECK(at(3, 5) == REV && at(1, 1) == REV && at(2, 2) == BG && at(3, 4) == BG);
	CHECK(at(0, 0) == RED && at(4, 5) == RED && at(rows - 1, cols - 1) == RED);

	/* text: a letter, a dim one, a string-face one, a wide reversed one */
	paint(BG);
	put(&g, 4, 1, "M", 1, ATTR_NONE);
	put(&g, 5, 1, "M", 1, ATTR_DIM);
	put(&g, 6, 1, "M", 1, ATTR_NONE);
	g.c[6 * g.cols + 1].face = FACE_STRING;
	put(&g, 7, 1, "\xe4\xb8\xad", 2, ATTR_REV);   /* U+4E2D, two cells */
	put(&g, 8, 1, " ", 1, ATTR_NONE);
	g.c[8 * g.cols + 1].face = FACE_COMMENT_TODO;   /* a face with a background */
	put(&g, 8, 2, " ", 1, ATTR_NONE);
	g.c[8 * g.cols + 2].bg = FACE_CURSOR_LINE;      /* a background layer */
	put(&g, 8, 3, " ", 1, ATTR_NONE);
	g.c[8 * g.cols + 3].ul = FACE_DIAG_UNDER_ERROR; /* an undercurl layer */
	put(&g, 8, 4, " ", 1, ATTR_NONE);
	g.c[8 * g.cols + 4].face = FACE_MARKUP_UNDERLINE;
	/* a cluster longer than a cell's 4 bytes, from the grid's store: its
	   accents draw over the letter */
	put(&g, 9, 1, "e", 1, ATTR_NONE);
	put(&g, 10, 1, "e\xcc\x81\xcc\x81\xcc\x81\xcc\x81", 1, ATTR_NONE);   /* 4 combining acutes */
	present(&g, &shown);
	/* glyph edges are blended (antialiased), so few pixels are exactly the
	   text colour: look at the inked pixels and the one furthest from the
	   background instead */
	COLORREF m = ink(4, 1), d = ink(5, 1), s = ink(6, 1);
	CHECK(inked(4, 1) > 0 && inked(4, 1) < cw * ch);   /* a letter, not a filled cell */
	CHECK(inked(5, 1) > 0 && lum(d) < lum(m));         /* dim is darker */
	CHECK(inked(6, 1) > 0 && GetGValue(s) > GetRValue(s) && GetGValue(s) > GetBValue(s));   /* green */
	CHECK(GetBValue(m) > GetGValue(m));                /* the text colour is blue-white, not green */
	CHECK(count(7, 2, REV) > cw * ch / 2 && count(7, 3, REV) == 0);
	CHECK(g.c[10 * g.cols + 1].len == 9 && inked(10, 1) > inked(9, 1));
	CHECK(count(8, 1, RGB(faces[FACE_COMMENT_TODO].bg[0], faces[FACE_COMMENT_TODO].bg[1], faces[FACE_COMMENT_TODO].bg[2])) == cw * ch);
	CHECK(count(8, 2, RGB(faces[FACE_CURSOR_LINE].bg[0], faces[FACE_CURSOR_LINE].bg[1], faces[FACE_CURSOR_LINE].bg[2])) == cw * ch);
	/* the undercurl: one pixel a column in its sp, a wave (not all on the
	   last row); the underline: the last row in the text's colour */
	const Face *sp = &faces[FACE_DIAG_UNDER_ERROR];
	COLORREF red = RGB(sp->sp[0], sp->sp[1], sp->sp[2]);
	int low = 0;
	for (int x = 3 * cw; x < 4 * cw; x++) low += px(x, 9 * ch - 1) == red;
	CHECK(count(8, 3, red) == cw && inked(8, 3) == cw && low < cw);
	CHECK(count(8, 4, REV) == cw && px(4 * cw, 9 * ch - 1) == REV);   /* REV: the text colour */

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
	b->present(0, &g, &shown);
	CHECK(!b->ready(0));
	do QueryPerformanceCounter(&t1); while (!b->ready(0));
	CHECK(p > 0 && t1.QuadPart - t0.QuadPart >= p + p / 8);
	printf("test_win (measured): next frame allowed after %.2f ms, refresh period %.2f ms\n",
	       (double)(t1.QuadPart - t0.QuadPart) * 1000 / (double)f.QuadPart, (double)p * 1000 / (double)f.QuadPart);
	present(&g, &shown);
	/* idle: after a frame with nothing left to draw the timer stays quiet */
	present(&g, &shown);
	CHECK(WaitForSingleObject((HANDLE)(INT_PTR)fds[1], 200) == WAIT_TIMEOUT);

	/* minimised: never ready; Ctrl-Z minimises (gvim) and main.c then
	   redraws whole: the cursor at (0, 0) is drawn too */
	b->suspend();
	CHECK(IsIconic(win) && !b->ready(0));
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
	b->size(0, &r2, &c2);
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
	b->size(0, &rows, &cols);
	CHECK(rows < r2 * 6 / 10 && cols < c2 * 6 / 10);   /* font sizes round, so not exactly half */
	CHECK(!grid_resize(&g, rows, cols));
	put(&g, 0, 0, " ", 1, ATTR_REV);
	g.crow = rows - 1;
	paint(RED);
	present(&g, &shown);
	int w2 = 0;
	while (px(w2, 0) == REV) w2++;
	CHECK(w2 >= 2 * cw - 2 && w2 <= 2 * cw + 2 && px(w2, 0) == BG && px(c2 * cw - 1, r2 * ch - 1) != RED);

	/* a wallpaper (image.h) dropped into the config folder shows from the
	   next size: tests/test_image.png (red then blue) covers the window, red
	   at its left edge, blue at its right, a fifth of it over the base, text
	   drawn over it, the cursor's cell opaque; then tests/test_wallpaper.png
	   (2 rows red above 6 blue) in a window twice as wide as high: covered,
	   its middle rows show, blue at the top (squeezed whole, red); in one
	   half as wide as high, all of it, which stays put when the text
	   scrolls (no rows copied, src/paint); taken
	   away, the base is back from the next size */
	cw = w2;
	for (ch = 0; px(0, ch) == REV; ch++) {}
	static const char *const pic[4] = { "tests\\test_image.png", "tests\\test_wallpaper.png", "tests\\test_wallpaper.png", NULL };
	for (int t = 0; t < 4; t++) {
		CHECK(pic[t] ? !_putenv("XDG_CONFIG_HOME=build\\win\\cfg") && (CreateDirectoryA("build\\win\\cfg", NULL) || GetLastError() == ERROR_ALREADY_EXISTS)
		                 && (CreateDirectoryA("build\\win\\cfg\\homeros", NULL) || GetLastError() == ERROR_ALREADY_EXISTS)
		                 && CopyFileA(pic[t], "build\\win\\cfg\\homeros\\wallpaper", FALSE)
		             : DeleteFileA("build\\win\\cfg\\homeros\\wallpaper"));
		RECT cr;
		GetWindowRect(win, &wr);
		GetClientRect(win, &cr);
		SetWindowPos(win, NULL, 0, 0, wr.right - wr.left + (t == 1 ? 0 : t == 2 ? cr.bottom / 2 - cr.right : t ? -cw : cw),
		             wr.bottom - wr.top + (t == 1 ? cr.right / 2 - cr.bottom : 0), SWP_NOMOVE | SWP_NOZORDER);
		n = input(k, 1024);
		resized = 0;
		for (int i = 0; i < n; i++) resized |= k[i] == KEY_RESIZE;
		CHECK(resized);
		b->size(0, &rows, &cols);
		CHECK(!grid_resize(&g, rows, cols));   /* the cursor at (0, 0) */
		put(&g, 0, 1, "x", 1, ATTR_NONE);
		present(&g, &shown);
		GetClientRect(win, &cr);
		int y = (rows - 1) * ch + ch / 2;
		COLORREF red = RGB(75, 24, 37), blue = RGB(24, 24, 88);
		CHECK(at(0, 0) == REV);
		CHECK(t == 1 ? px(cr.right / 2, ch + 1) == blue && cr.right / 2 == cr.bottom
		      : t == 2 ? px(1, ch + 1) == red && px(1, y) == blue   /* half as wide as high: all of it */
		             : px(1, y) == (t ? BG : red) && px(cr.right - 2, y) == (t ? BG : blue));
		CHECK(px(cw, 0) == px(1, ch + 1) && count(0, 1, px(cw, 0)) < cw * ch);   /* the x, over the wallpaper */
		put(&g, 0, 1, " ", 1, ATTR_NONE);
		present(&g, &shown);
		CHECK(count(0, 1, px(cw, 0)) == cw * ch);   /* and gone */
		put(&g, 0, 2, "ä¸­", 2, ATTR_NONE);   /* U+4E2D, two cells */
		put(&g, 0, 5, "å­", 2, ATTR_NONE);   /* U+5B57 */
		present(&g, &shown);
		put(&g, 0, 2, "å­", 2, ATTR_NONE);   /* the first becomes the second, both halves */
		present(&g, &shown);
		int same = 0;
		for (int i = 0; i < 2 * cw * ch; i++) same += px(2 * cw + i % (2 * cw), i / (2 * cw)) == px(5 * cw + i % (2 * cw), i / (2 * cw));
		CHECK(same == 2 * cw * ch);
		COLORREF was[64];
		for (int s = 0; t == 2 && s < 2; s++) {   /* lines, then the same moved up a row */
			for (int r = 1; r < rows; r++) {
				char l[16];
				grid_puts(&g, r, 2, cols - 2, l, (size_t)snprintf(l, sizeof l, "line %d", r + s), ATTR_NONE, FACE_NONE);
			}
			present(&g, &shown);
			for (int q = 1; q < rows && q < 64; q++) {
				if (!s) was[q] = px(1, q * ch + ch / 2);
				CHECK(px(1, q * ch + ch / 2) == was[q]);
			}
		}
	}
	/* frames: a second window (tab page 1) gets the keyboard, a focus
	   change from it; its keys and its pacing are its own */
	input(k, 1024);
	wait_ready();
	CHECK(!b->win_open(1));
	HWND w1 = NULL;
	while ((w1 = FindWindowExW(NULL, w1, L"homeros", NULL)) == win) {}
	CHECK(w1 && !IsZoomed(w1));   /* a new frame at the default size */
	n = input(k, 1024);
	int f1 = -1, nf = 0;
	for (int i = 0; i < n; i++) if (k[i] == KEY_FOCUS) { f1 = from[i]; nf++; }
	CHECK(nf == 1 && f1 == 1);
	PostMessageW(w1, WM_CHAR, 'x', 0);
	n = input(k, 1024);
	CHECK(n >= 1 && k[n - 1] == 'x' && from[n - 1] == 1);
	Grid g1 = { 0 }, shown1 = { 0 };
	b->size(1, &r2, &c2);
	CHECK(!grid_resize(&g1, r2, c2) && b->ready(1) && b->ready(0));
	b->present(1, &g1, &shown1);
	CHECK(!b->ready(1) && b->ready(0));
	/* one timer for both, set for the sooner frame: window 0's, drawn 8 ms
	   before window 1's, though window 1 asked last */
	while (!b->ready(1)) Sleep(1);
	Sleep(2);
	WaitForSingleObject((HANDLE)(INT_PTR)fds[1], 0);   /* the wake that timer gave, which the loop takes */
	QueryPerformanceCounter(&t0);
	b->present(0, &g, &shown);
	Sleep(8);
	b->present(1, &g1, &shown1);
	CHECK(!b->ready(0) && !b->ready(1));
	CHECK(WaitForSingleObject((HANDLE)(INT_PTR)fds[1], 1000) == WAIT_OBJECT_0);
	QueryPerformanceCounter(&t1);
	CHECK(t1.QuadPart - t0.QuadPart < p + p / 8 + f.QuadPart * 4 / 1000 && b->ready(0));
	/* SPC 5 o: window 0 raised; its focus comes before KEY_RAISED (Focus.tla) */
	b->raise(0);
	n = input(k, 1024);
	int f0 = -1, r0 = -1;
	for (int i = 0; i < n; i++) {
		if (k[i] == KEY_FOCUS && from[i] == 0 && f0 < 0) f0 = i;
		if (k[i] == KEY_RAISED && from[i] == -1) r0 = i;
	}
	CHECK(f0 >= 0 && r0 > f0 && GetForegroundWindow() == win);
	/* another program in front and back: no focus change */
	HWND other = CreateWindowExW(0, L"STATIC", L"other", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 100, 100, NULL, NULL, NULL, NULL);
	CHECK(other && SetForegroundWindow(other));
	input(k, 1024);
	CHECK(SetForegroundWindow(win));
	n = input(k, 1024);
	for (int i = 0; i < n; i++) CHECK(k[i] != KEY_FOCUS);
	DestroyWindow(other);
	b->win_close(1);
	CHECK(!IsWindow(w1) && IsWindow(win));
	grid_free(&g1);
	grid_free(&shown1);

	b->close();
	CHECK(!FindWindowW(L"homeros", NULL));
	grid_free(&g);
	grid_free(&shown);
	puts("test_win: keys, mouse and clipboard; identical frame draws nothing; one change draws its row; "
	     "text, dim, face, face background, long cluster, wide and image cells; cursor drawn, restored and at (0,0) after a whole redraw; WM_PAINT; "
	     "resize and DPI redraw whole; wallpaper; close is a key; paced; idle quiet; minimised draws nothing; "
	     "a second window: its focus, keys and pacing, raised back with KEY_RAISED after the focus");
	return 0;
}
#else
#include <stdio.h>
int main(void) { puts("test_win: Windows only (make win runs it under Wine)"); return 0; }
#endif
