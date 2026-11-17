/* Win32 backend (package 9): a window that shows the cell grid from a DIB
   section, the Wayland backend's twin (DESIGN.md, "Window, Windows").
   Until font.h (package 7) exists, glyphs come from GDI's ExtTextOutW into
   the DIB; package 7's FreeType and HarfBuzz code replaces draw_cell.

   When to draw: a GDI window gets no frame callback.  DWM composes about
   every refresh period P; if one composition comes at most D after P from
   the one before, two frames drawn more than P + D apart always have a
   composition between them, so neither is drawn for nothing (Pace.tla:
   proved for P 2 to 4, D 0 to 1, every gap; drawing whenever there is
   something new, or exactly P apart, wastes frames).  So a frame may be
   drawn once P + P/8 has passed since the last (D under P/8 is a
   hypothesis to measure on Windows), and a waitable timer wakes the loop
   then.  Like a hidden Wayland window, a minimised one draws nothing.

   Without _WIN32 this file is empty, so the Linux build never sees it. */
#ifdef _WIN32
#include <windows.h>
#include <dwmapi.h>
#include "backend.h"
#include "keys.h"
#include "uni.h"

enum { RING = 256, PT = 12 };   /* font size in points: a guess until package 7 */

static HWND win;
static HDC mem;                 /* holds the DIB, which holds what is shown */
static HBITMAP dib, nodib;
static HFONT font, nofont;
static HANDLE timer;
static int cw, ch, dpi;         /* cell size in pixels */
static int dw, dh, stale;       /* the DIB's size; stale: redraw it whole */
static int ring[RING], rh, rn;  /* keys the window procedure turned out */
static int resized;
static LONGLONG period, next;   /* QueryPerformanceCounter ticks */
static WCHAR surr;               /* a high surrogate waiting for its low one */

/* Catppuccin Mocha (FEATURES.md); attr -> foreground, background */
static const COLORREF fg[] = { RGB(205, 214, 244), RGB(108, 112, 134), RGB(30, 30, 46) };
static const COLORREF bg[] = { RGB(30, 30, 46), RGB(30, 30, 46), RGB(205, 214, 244) };

static void put(int k)
{
	if (rn < RING) ring[(rh + rn++) % RING] = k;
}

static void measure_period(void)
{
	DWM_TIMING_INFO t = { .cbSize = sizeof t };
	/* DWM is always on since Windows 8; without it, no pacing, as a terminal */
	period = SUCCEEDED(DwmGetCompositionTimingInfo(NULL, &t)) ? (LONGLONG)t.qpcRefreshPeriod : 0;
}

static int make_font(void)
{
	HFONT f = CreateFontW(-MulDiv(PT, dpi, 72), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
	                      OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
	                      FIXED_PITCH | FF_MODERN, L"JetBrains Mono");
	TEXTMETRICW tm;
	if (!f) return -1;
	SelectObject(mem, f);
	if (font) DeleteObject(font);
	font = f;
	GetTextMetricsW(mem, &tm);
	cw = tm.tmAveCharWidth;
	ch = tm.tmHeight;
	return 0;
}

/* A DIB the size of the client area, cleared to the background: what a
   shown grid of spaces looks like (grid_resize clears to spaces). */
static int make_dib(void)
{
	RECT r;
	GetClientRect(win, &r);
	BITMAPINFO bi = { .bmiHeader = { .biSize = sizeof bi.bmiHeader, .biWidth = r.right ? r.right : 1,
	                  .biHeight = -(r.bottom ? r.bottom : 1), .biPlanes = 1, .biBitCount = 32 } };
	void *px;
	HBITMAP b = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &px, NULL, 0);
	if (!b) return -1;
	SelectObject(mem, b);
	if (dib) DeleteObject(dib);
	dib = b;
	SetBkColor(mem, bg[ATTR_NONE]);
	ExtTextOutW(mem, 0, 0, ETO_OPAQUE, &r, NULL, 0, NULL);
	dw = r.right;
	dh = r.bottom;
	return 0;
}

static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
	switch (m) {
	case WM_CHAR:
		if (IS_HIGH_SURROGATE(w)) { surr = (WCHAR)w; return 0; }
		if (IS_LOW_SURROGATE(w)) {
			if (surr) put(0x10000 + ((surr - 0xD800) << 10) + ((int)w - 0xDC00));
			surr = 0;
			return 0;
		}
		surr = 0;
		/* the Backspace key gives 8 like Ctrl-H; a terminal sends 127 for it */
		put(w == 8 && GetKeyState(VK_CONTROL) >= 0 ? KEY_BS : (int)w);
		return 0;
	case WM_KEYDOWN:
		switch (w) {
		case VK_LEFT: put(KEY_LEFT); return 0;
		case VK_RIGHT: put(KEY_RIGHT); return 0;
		case VK_UP: put(KEY_UP); return 0;
		case VK_DOWN: put(KEY_DOWN); return 0;
		}
		break;
	case WM_SIZE:
		if (w != SIZE_MINIMIZED && (LOWORD(l) != dw || HIWORD(l) != dh)) resized = stale = 1;
		return 0;
	case WM_DPICHANGED: {
		const RECT *r = (const RECT *)l;
		dpi = HIWORD(w);
		make_font();
		SetWindowPos(h, NULL, r->left, r->top, r->right - r->left, r->bottom - r->top,
		             SWP_NOZORDER | SWP_NOACTIVATE);
		resized = stale = 1;
		return 0;
	}
	case WM_DISPLAYCHANGE:
		measure_period();
		break;
	case WM_PAINT: {
		PAINTSTRUCT ps;
		HDC dc = BeginPaint(h, &ps);
		RECT *r = &ps.rcPaint;
		BitBlt(dc, r->left, r->top, r->right - r->left, r->bottom - r->top, mem, r->left, r->top, SRCCOPY);
		EndPaint(h, &ps);
		return 0;
	}
	case WM_ERASEBKGND:
		return 1;
	case WM_CLOSE:   /* keys.h has no key for it: quit with :q */
		return 0;
	}
	return DefWindowProcW(h, m, w, l);
}

int backend_open(void)
{
	WNDCLASSW wc = { .lpfnWndProc = proc, .hInstance = GetModuleHandleW(NULL),
	                 .hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_IBEAM), .lpszClassName = L"homeros" };
	SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
	if (!RegisterClassW(&wc)) return -1;
	win = CreateWindowExW(0, L"homeros", L"homeros", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
	                      CW_USEDEFAULT, CW_USEDEFAULT, NULL, NULL, wc.hInstance, NULL);
	if (!win) return -1;
	dpi = (int)GetDpiForWindow(win);
	mem = CreateCompatibleDC(NULL);
	timer = CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
	if (!mem || !timer) { backend_close(); return -1; }
	nofont = SelectObject(mem, GetStockObject(SYSTEM_FONT));
	nodib = GetCurrentObject(mem, OBJ_BITMAP);
	if (make_font() || make_dib()) { backend_close(); return -1; }
	measure_period();
	ShowWindow(win, SW_SHOWDEFAULT);
	resized = stale = 0;   /* the size backend_size gives now is the first one */
	return 0;
}

void backend_close(void)
{
	if (mem) { SelectObject(mem, nofont); SelectObject(mem, nodib); DeleteDC(mem); }
	if (font) DeleteObject(font);
	if (dib) DeleteObject(dib);
	if (timer) CloseHandle(timer);
	if (win) DestroyWindow(win);
	UnregisterClassW(L"homeros", GetModuleHandleW(NULL));
	mem = NULL; font = NULL; dib = NULL; timer = NULL; win = NULL;
	dw = dh = 0;
}

/* -1 is the thread's message queue (loop.h, pending thread 0); a handle's
   low 32 bits are all of it (Microsoft, 32/64-bit interprocess handles). */
int backend_fds(int fds[2])
{
	fds[0] = -1;
	fds[1] = (int)(INT_PTR)timer;
	return 2;
}

int backend_input(int *out, int n)
{
	MSG m;
	/* each message gives at most one key, so stop while the ring has room */
	while (rn < RING && PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) {
		TranslateMessage(&m);
		DispatchMessageW(&m);
	}
	int k = 0;
	if (resized && n > 0) { resized = 0; out[k++] = KEY_RESIZE; }
	for (; k < n && rn; rn--, rh = (rh + 1) % RING) out[k++] = ring[rh];
	return k;
}

void backend_size(int *rows, int *cols)
{
	RECT r;
	GetClientRect(win, &r);
	*rows = r.bottom / ch > 0 ? r.bottom / ch : 1;
	*cols = r.right / cw > 0 ? r.right / cw : 1;
}

int backend_ready(void)
{
	LARGE_INTEGER now, f, due;
	if (IsIconic(win)) return 0;
	QueryPerformanceCounter(&now);
	if (now.QuadPart >= next) return 1;
	/* set again on every call: the timer and the counter are different
	   clocks, so a timer that fired a little early must not leave the loop
	   waiting forever.  Relative, in 100 ns units, rounded up. */
	QueryPerformanceFrequency(&f);
	due.QuadPart = -(((next - now.QuadPart) * 10000000 + f.QuadPart - 1) / f.QuadPart);
	SetWaitableTimer(timer, &due, 0, NULL, NULL, FALSE);
	return 0;
}

static void draw_cell(const Grid *g, int r, int c, int cursor)
{
	const Cell *x = &g->c[r * g->cols + c];
	unsigned cp = uni_cp(x->ch, x->len, 0);
	WCHAR u[2] = { (WCHAR)cp };
	UINT n = 1;
	int a = cursor ? ATTR_REV : x->attr, wide = c + 1 < g->cols && !x[1].len;
	RECT rc = { c * cw, r * ch, (c + 1 + wide) * cw, (r + 1) * ch };
	if (cp >= 0x10000) {   /* UTF-16 surrogates */
		u[0] = (WCHAR)(0xD800 + (cp - 0x10000) / 0x400);
		u[1] = (WCHAR)(0xDC00 + (cp - 0x10000) % 0x400);
		n = 2;
	}
	SetTextColor(mem, fg[a]);
	SetBkColor(mem, bg[a]);
	ExtTextOutW(mem, rc.left, rc.top, ETO_OPAQUE | ETO_CLIPPED, &rc, u, n, NULL);
}

void backend_present(const Grid *g, Grid *shown)
{
	int all = stale || shown->rows != g->rows || shown->cols != g->cols;
	if (all && (grid_resize(shown, g->rows, g->cols) || make_dib())) return;
	stale = 0;
	HDC dc = GetDC(win);
	int pr = shown->crow, pc = shown->ccol;
	for (int r = 0; r < g->rows; r++) {
		int lo = g->cols, hi = 0;
		for (int c = 0; c < g->cols; c++) {
			const Cell *x = &g->c[r * g->cols + c];
			Cell *y = &shown->c[r * g->cols + c];
			int cur = r == g->crow && c == g->ccol, was = r == pr && c == pc;
			if (grid_cell_eq(x, y) && cur == was) continue;
			*y = *x;
			if (!x->len) continue;   /* the right half of a wide character */
			draw_cell(g, r, c, cur);
			if (c < lo) lo = c;
			hi = c + 1 + (c + 1 < g->cols && !x[1].len);
		}
		if (hi && !all) BitBlt(dc, lo * cw, r * ch, (hi - lo) * cw, ch, mem, lo * cw, r * ch, SRCCOPY);
	}
	if (all) BitBlt(dc, 0, 0, dw, dh, mem, 0, 0, SRCCOPY);
	ReleaseDC(win, dc);
	shown->crow = g->crow;
	shown->ccol = g->ccol;
	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);
	next = now.QuadPart + period + period / 8;
}

/* gvim: "In the GUI, suspending is implemented as iconising gvim" */
void backend_suspend(void) { ShowWindow(win, SW_MINIMIZE); }
#endif
