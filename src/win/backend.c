/* Win32 backend (package 9): a window that shows the cell grid from a DIB
   section, the Wayland backend's twin (DESIGN.md, "Window, Windows").
   Glyphs come from GDI's ExtTextOutW into the DIB until package 7's
   FreeType and HarfBuzz code (font.h) builds on Windows; it replaces
   draw_cell.

   When to draw: a GDI window gets no frame callback.  DWM composes about
   every refresh period P; if one composition comes at most D after P from
   the one before, two frames drawn more than P + D apart always have a
   composition between them, so neither is drawn for nothing (Pace.tla,
   and design/specs/pace-sweep.sh for P 2 to 4, D 0 to 1, every gap;
   drawing whenever there is something new, or exactly P apart, wastes
   frames; without the timer the last key can wait for the next input).
   So a frame may be drawn once P + P/8 has passed since the last (D under
   P/8 is a hypothesis to measure on Windows), and a waitable timer wakes
   the loop then.  Like a hidden Wayland window, a minimised one draws nothing.

   Without _WIN32 this file is empty, so the Linux build never sees it. */
#ifdef _WIN32
#include <windows.h>
#include <dwmapi.h>
#include "backend.h"
#include "keys.h"
#include "loop.h"
#include "uni.h"
#include <windowsx.h>

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
static WCHAR surr;              /* a high surrogate waiting for its low one */
static int mrow, mcol, wheel;   /* the last mouse key's cell; wheel delta not yet a key */

/* Catppuccin Mocha (FEATURES.md): the window's own text, dim (overlay0)
   and base, which a terminal takes from its settings; the faces' colours
   come from grid.h's faces[].  Their styles (bold, italic, underline,
   strike) wait for package 7's fonts. */
static const COLORREF TEXT = RGB(205, 214, 244), DIMC = RGB(108, 112, 134), BASE = RGB(30, 30, 46);

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

/* The first installed of JetBrains Mono's Nerd Font builds (DESIGN.md), for
   the icons, then plain JetBrains Mono: GDI substitutes any font for a
   missing family, so the face it gave is compared with the one asked.
   Greyscale smoothing: ClearType's coloured fringes made text look blue on
   the dark background on Davy's laptop. */
static int make_font(void)
{
	static const WCHAR *const family[] = { L"JetBrainsMono Nerd Font Mono", L"JetBrainsMono NFM",
	                                       L"JetBrainsMono Nerd Font", L"JetBrains Mono" };
	HFONT f = NULL;
	for (unsigned i = 0; i < sizeof family / sizeof *family; i++) {
		WCHAR face[LF_FACESIZE];
		if (f) DeleteObject(f);
		f = CreateFontW(-MulDiv(PT, dpi, 72), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
		                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
		                FIXED_PITCH | FF_MODERN, family[i]);
		if (!f) return -1;
		HGDIOBJ old = SelectObject(mem, f);
		GetTextFaceW(mem, LF_FACESIZE, face);
		SelectObject(mem, old);
		if (!lstrcmpiW(face, family[i])) break;
	}
	TEXTMETRICW tm;
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
	SetBkColor(mem, BASE);
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
		case VK_DELETE: put(KEY_DEL); return 0;
		}
		break;
	case WM_LBUTTONDOWN:
		mrow = GET_Y_LPARAM(l) / ch;
		mcol = GET_X_LPARAM(l) / cw;
		put(KEY_MOUSE_LEFT);
		return 0;
	case WM_MOUSEWHEEL: {   /* one key per notch (WHEEL_DELTA), as Neovim's GUIs */
		POINT pt = { GET_X_LPARAM(l), GET_Y_LPARAM(l) };   /* screen coordinates */
		ScreenToClient(h, &pt);
		mrow = pt.y / ch;
		mcol = pt.x / cw;
		for (wheel += GET_WHEEL_DELTA_WPARAM(w); wheel >= WHEEL_DELTA && rn < RING; wheel -= WHEEL_DELTA)
			put(KEY_WHEEL_UP);
		for (; wheel <= -WHEEL_DELTA && rn < RING; wheel += WHEEL_DELTA) put(KEY_WHEEL_DOWN);
		return 0;
	}
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
	case WM_CLOSE:   /* the editor decides (:qa), not DefWindowProc */
		put(KEY_CLOSE);
		return 0;
	}
	return DefWindowProcW(h, m, w, l);
}

static void win_close(void)
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

static int win_open(void)
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
	if (!mem || !timer) { win_close(); return -1; }
	nofont = SelectObject(mem, GetStockObject(SYSTEM_FONT));
	nodib = GetCurrentObject(mem, OBJ_BITMAP);
	if (make_font() || make_dib()) { win_close(); return -1; }
	measure_period();
	ShowWindow(win, SW_SHOWDEFAULT);
	resized = stale = 0;   /* the size backend_size gives now is the first one */
	return 0;
}

/* loop.h: a handle cut to int (its low 32 bits are all of it) */
static int win_fds(int fds[2])
{
	fds[0] = LOOP_MSGQ;
	fds[1] = (int)(INT_PTR)timer;
	return 2;
}

static int win_input(int *out, int n)
{
	MSG m;
	/* each message gives at most one key (the wheel stops at a full ring),
	   so stop while the ring has room; what stays queued still wakes the
	   loop, which waits with MWMO_INPUTAVAILABLE (asked of thread 0 for
	   loop.h) */
	while (rn < RING && PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) {
		TranslateMessage(&m);
		DispatchMessageW(&m);
	}
	int k = 0;
	if (resized && n > 0) { resized = 0; out[k++] = KEY_RESIZE; }
	for (; k < n && rn; rn--, rh = (rh + 1) % RING) out[k++] = ring[rh];
	return k;
}

static void win_size(int *rows, int *cols)
{
	RECT r;
	GetClientRect(win, &r);
	*rows = r.bottom / ch > 0 ? r.bottom / ch : 1;
	*cols = r.right / cw > 0 ? r.right / cw : 1;
}

static int win_ready(void)
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
	const unsigned char *s = grid_text(x);
	WCHAR u[GRID_CLUSTER_MAX];   /* UTF-16 takes no more units than UTF-8 bytes */
	UINT n = 0;
	for (size_t i = 0; i < x->len; i += uni_len(s, x->len, i)) {   /* the whole cluster */
		unsigned cp = uni_cp(s, x->len, i);
		if (cp >= 0x10000) {   /* UTF-16 surrogates */
			u[n++] = (WCHAR)(0xD800 + (cp - 0x10000) / 0x400);
			u[n++] = (WCHAR)(0xDC00 + (cp - 0x10000) % 0x400);
		} else u[n++] = (WCHAR)cp;
	}
	int wide = c + 1 < g->cols && !x[1].len;
	const Face *fa = &faces[x->face < FACE_COUNT ? x->face : FACE_NONE];
	COLORREF f = x->attr == ATTR_DIM ? DIMC : fa->style & FACE_FG ? RGB(fa->fg[0], fa->fg[1], fa->fg[2]) : TEXT;
	COLORREF b = fa->style & FACE_BG ? RGB(fa->bg[0], fa->bg[1], fa->bg[2]) : BASE;
	RECT rc = { c * cw, r * ch, (c + 1 + wide) * cw, (r + 1) * ch };
	/* reversed (status line, cursor): the colours swap, as a terminal's SGR 7 */
	int rev = cursor || x->attr == ATTR_REV;
	SetTextColor(mem, rev ? b : f);
	SetBkColor(mem, rev ? f : b);
	ExtTextOutW(mem, rc.left, rc.top, ETO_OPAQUE | ETO_CLIPPED, &rc, u, n, NULL);
}

static void win_present(const Grid *g, Grid *shown)
{
	int all = stale || shown->rows != g->rows || shown->cols != g->cols;
	if (all && (grid_resize(shown, g->rows, g->cols) || make_dib())) return;
	stale = 0;
	HDC dc = GetDC(win);
	/* after a whole redraw the DIB shows no cursor anywhere */
	int pr = all ? -1 : shown->crow, pc = shown->ccol;
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
static void win_suspend(void) { ShowWindow(win, SW_MINIMIZE); }

/* The clipboard holds UTF-16 with CRLF; the registers hold UTF-8 with LF.
   Neovim on Windows converts the same way (win32yank -i --crlf, -o --lf). */
static int win_clip_set(const char *s, size_t n)
{
	int lf = 0, w = n ? MultiByteToWideChar(CP_UTF8, 0, s, (int)n, NULL, 0) : 0;
	for (size_t i = 0; i < n; i++) lf += s[i] == '\n';
	if (n && !w) return -1;
	HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, ((size_t)w + (size_t)lf + 1) * sizeof(WCHAR));
	WCHAR *u = h ? GlobalLock(h) : NULL;
	if (!u) { if (h) GlobalFree(h); return -1; }
	MultiByteToWideChar(CP_UTF8, 0, s, (int)n, u + lf, w);   /* then spread out in place */
	int j = 0;
	for (int i = lf; i < w + lf; i++) {
		if (u[i] == '\n') u[j++] = '\r';
		u[j++] = u[i];
	}
	u[j] = 0;
	GlobalUnlock(h);
	if (!OpenClipboard(win)) { GlobalFree(h); return -1; }
	EmptyClipboard();
	int ok = SetClipboardData(CF_UNICODETEXT, h) != NULL;
	CloseClipboard();
	if (!ok) GlobalFree(h);   /* on success the clipboard owns it */
	return ok ? 0 : -1;
}

static long win_clip_get(char *buf, size_t max)
{
	if (!OpenClipboard(win)) return -1;
	HANDLE h = GetClipboardData(CF_UNICODETEXT);
	const WCHAR *u = h ? GlobalLock(h) : NULL;
	long n = -1;
	if (u) {
		n = 0;
		for (int i = 0; u[i]; i++) {
			if (u[i] == '\r' && u[i + 1] == '\n') continue;
			char b[4];
			int k = WideCharToMultiByte(CP_UTF8, 0, u + i, IS_HIGH_SURROGATE(u[i]) && u[i + 1] ? 2 : 1, b, 4, NULL, NULL);
			for (int j = 0; j < k; j++, n++) if ((size_t)n < max) buf[n] = b[j];
			i += IS_HIGH_SURROGATE(u[i]) && u[i + 1];
		}
		GlobalUnlock(h);
	}
	CloseClipboard();
	return n;
}

static void win_mouse(int *row, int *col) { *row = mrow; *col = mcol; }

const Backend backend_win = {
	win_open, win_close, win_fds, win_input, win_size, win_ready,
	win_present, win_suspend, win_clip_set, win_clip_get, win_mouse
};
#endif
