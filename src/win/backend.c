/* Win32 backend (package 9): a window that shows the cell grid from a DIB
   section, the Wayland backend's twin (DESIGN.md, "Window, Windows"): the
   same painter (paint.h) draws into it with the same FreeType and
   HarfBuzz code (font.h), DirectWrite only finding the fonts, as WezTerm
   draws on Windows (GDI's text had no fallback: ✦ and ↻ were boxes).

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
#include "font.h"
#include "image.h"
#include "keys.h"
#include "loop.h"
#include "paint.h"
#include <windowsx.h>

enum { RING = 256, PT = 12 };   /* font size in points (hypothesis: Davy's size is not recorded) */
/* JetBrains Mono's Nerd Font builds first (DESIGN.md), for the icons */
static const char FAMILY[] = "JetBrainsMono Nerd Font Mono,JetBrainsMono NFM,JetBrainsMono Nerd Font,JetBrains Mono";

/* A window per tab page, each with its monitor's DPI (so its own font
   size) and refresh period (so its own pacing) */
static struct win {
	int id;                 /* its tab page (backend.h's id) */
	HWND h;
	HMONITOR mon;           /* the monitor its period was measured on */
	HDC mem;                /* holds the DIB, which holds what is shown */
	HBITMAP dib, nodib;
	uint32_t *bits;         /* the DIB's pixels */
	HDC wdc;                /* holds the wallpaper at the window's size (image.h), if any */
	HBITMAP wdib, nowdib;
	const uint32_t *wbits;  /* its pixels, the painter's back (paint.h) */
	int cw, ch, dpi;        /* cell size in pixels */
	int dw, dh, stale;      /* the DIB's size; stale: redraw it whole */
	int resized;
	int sizing;             /* 1 while the user drags an edge, 2 once the size changed */
	LONGLONG period, next;  /* QueryPerformanceCounter ticks */
	struct win *link;
} *wins;
static HANDLE timer;
static LONGLONG armed;          /* when the timer is set for */
static int ring[RING][2], rh, rn;   /* keys the window procedure turned out, with their window */
static int focused = -1;        /* the last of ours that had the keyboard */
static WCHAR surr;              /* a high surrogate waiting for its low one */
static int mrow, mcol, wheel;   /* the last mouse key's cell; wheel delta not yet a key */

static struct win *win_of(int id) { struct win *w = wins; while (w && w->id != id) w = w->link; return w; }

static void put(int k, int id)
{
	if (rn < RING) { int *q = ring[(rh + rn++) % RING]; q[0] = k; q[1] = id; }
}

/* The refresh period of the monitor w is on: DWM's clock is the primary
   monitor's only (research/frames-os.md), so the period comes from the
   QueryDisplayConfig path whose source is that monitor; DWM's when none
   matches.  DWM is always on since Windows 8; without it, no pacing, as a
   terminal. */
static void measure_period(struct win *w)
{
	DWM_TIMING_INFO t = { .cbSize = sizeof t };
	MONITORINFOEXW mi = { .cbSize = sizeof mi };
	UINT32 np, nm;
	LARGE_INTEGER f;
	w->period = SUCCEEDED(DwmGetCompositionTimingInfo(NULL, &t)) ? (LONGLONG)t.qpcRefreshPeriod : 0;
	w->mon = MonitorFromWindow(w->h, MONITOR_DEFAULTTONEAREST);
	if (!GetMonitorInfoW(w->mon, (MONITORINFO *)&mi) || GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &np, &nm)) return;
	DISPLAYCONFIG_PATH_INFO *p = malloc(np * sizeof *p);
	DISPLAYCONFIG_MODE_INFO *m = malloc(nm * sizeof *m);
	QueryPerformanceFrequency(&f);
	if (p && m && !QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &np, p, &nm, m, NULL))
		for (UINT32 i = 0; i < np; i++) {
			DISPLAYCONFIG_SOURCE_DEVICE_NAME s = { .header = { DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME, sizeof s,
			                                       p[i].sourceInfo.adapterId, p[i].sourceInfo.id } };
			DISPLAYCONFIG_RATIONAL r = p[i].targetInfo.refreshRate;
			if (r.Numerator && !DisplayConfigGetDeviceInfo(&s.header) && !lstrcmpW(s.viewGdiDeviceName, mi.szDevice)) {
				w->period = f.QuadPart * (LONGLONG)r.Denominator / (LONGLONG)r.Numerator;
				break;
			}
		}
	free(p);
	free(m);
}

/* The font at w's DPI, made the one in use: a size still open costs
   nothing (font.h), so each window's present calls it */
static int make_font(struct win *w)
{
	if (font_open(FAMILY, MulDiv(PT, w->dpi, 72))) return -1;
	int b;
	font_cell(&w->cw, &w->ch, &b);
	return 0;
}

/* A DIB the size of the client area, cleared to the background: what a
   shown grid of spaces looks like (grid_resize clears to spaces). */
static int make_dib(struct win *w)
{
	RECT r;
	GetClientRect(w->h, &r);
	BITMAPINFO bi = { .bmiHeader = { .biSize = sizeof bi.bmiHeader, .biWidth = r.right ? r.right : 1,
	                  .biHeight = -(r.bottom ? r.bottom : 1), .biPlanes = 1, .biBitCount = 32 } };
	void *px;
	HBITMAP b = CreateDIBSection(w->mem, &bi, DIB_RGB_COLORS, &px, NULL, 0);
	if (!b) return -1;
	SelectObject(w->mem, b);
	if (w->dib) DeleteObject(w->dib);
	w->dib = b;
	w->bits = px;
	w->dw = r.right;
	w->dh = r.bottom;
	/* the wallpaper's part that covers the window, scaled by GDI straight
	   from image.h's pixels; HALFTONE (smooth) costs 12 to 81 ms a size,
	   COLORONCOLOR 3 to 11 (measured, src/image/README.md), so the cheap
	   one while an edge is dragged and the smooth one when it is let go */
	if (w->wdib) { SelectObject(w->wdc, w->nowdib); DeleteObject(w->wdib); w->wdib = NULL; }
	w->wbits = NULL;
	int iw = 0, ih = 0, src[4];
	unsigned v;
	const uint32_t *wp = image_wallpaper(w->dw, w->dh, PAINT_BASE, &iw, &ih, src, &v);
	BITMAPINFO wi = { .bmiHeader = { .biSize = sizeof wi.bmiHeader, .biWidth = iw, .biHeight = -ih, .biPlanes = 1, .biBitCount = 32 } };
	void *wpx;
	if (wp && (w->wdib = CreateDIBSection(w->mem, &bi, DIB_RGB_COLORS, &wpx, NULL, 0))) {
		SelectObject(w->wdc, w->wdib);
		SetStretchBltMode(w->wdc, w->sizing ? COLORONCOLOR : HALFTONE);
		SetBrushOrgEx(w->wdc, 0, 0, NULL);   /* StretchBlt's rule after HALFTONE */
		/* a top-down DIB's source rows count from its bottom (measured) */
		StretchDIBits(w->wdc, 0, 0, w->dw, w->dh, src[0], ih - src[1] - src[3], src[2], src[3], wp, &wi, DIB_RGB_COLORS, SRCCOPY);
		GdiFlush();   /* before the painter reads it */
		w->wbits = wpx;
	}
	paint_clear(w->bits, w->dw, w->wbits, w->dw, w->dw, w->dh);
	return 0;
}

/* A window is registered in WM_NCCREATE, from CreateWindowExW's last
   argument: focus messages come before CreateWindowExW returns (measured
   under Wine, 4 runs, research/frames-os.md).  The few messages before it
   (WM_GETMINMAXINFO) go to DefWindowProc. */
static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
	struct win *x = (struct win *)GetWindowLongPtrW(h, GWLP_USERDATA);
	if (m == WM_NCCREATE) {
		x = ((CREATESTRUCTW *)l)->lpCreateParams;
		x->h = h;
		SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)x);
	}
	if (!x) return DefWindowProcW(h, m, w, l);
	switch (m) {
	case WM_CHAR:
		if (IS_HIGH_SURROGATE(w)) { surr = (WCHAR)w; return 0; }
		if (IS_LOW_SURROGATE(w)) {
			if (surr) put(0x10000 + ((surr - 0xD800) << 10) + ((int)w - 0xDC00), x->id);
			surr = 0;
			return 0;
		}
		surr = 0;
		/* the Backspace key gives 8 like Ctrl-H, a terminal sends 127 for it;
		   Tab gives 9 with Shift too */
		put(w == 8 && GetKeyState(VK_CONTROL) >= 0 ? KEY_BS : w == 9 && GetKeyState(VK_SHIFT) < 0 ? KEY_S_TAB : (int)w, x->id);
		return 0;
	case WM_SYSKEYDOWN:   /* Alt+Break; every other Alt key stays the system's (Alt+F4) */
		if (w == VK_PAUSE) { put(KEY_BREAK, x->id); return 0; }
		break;
	case WM_KEYDOWN:
		switch (w) {
		case VK_LEFT: put(KEY_LEFT, x->id); return 0;
		case VK_RIGHT: put(KEY_RIGHT, x->id); return 0;
		case VK_UP: put(KEY_UP, x->id); return 0;
		case VK_DOWN: put(KEY_DOWN, x->id); return 0;
		case VK_DELETE: put(KEY_DEL, x->id); return 0;
		case VK_F5: put(KEY_F5, x->id); return 0;
		case VK_PAUSE: put(KEY_BREAK, x->id); return 0;
		/* no WM_CHAR follows Ctrl+. (no control character has a dot) */
		case VK_OEM_PERIOD: if (GetKeyState(VK_CONTROL) < 0) { put(KEY_CTRL_DOT, x->id); return 0; } break;
		}
		break;
	case WM_LBUTTONDOWN:
		mrow = GET_Y_LPARAM(l) / x->ch;
		mcol = GET_X_LPARAM(l) / x->cw;
		put(KEY_MOUSE_LEFT, x->id);
		return 0;
	case WM_MOUSEWHEEL: {   /* one key per notch (WHEEL_DELTA), as Neovim's GUIs */
		POINT pt = { GET_X_LPARAM(l), GET_Y_LPARAM(l) };   /* screen coordinates */
		ScreenToClient(h, &pt);
		mrow = pt.y / x->ch;
		mcol = pt.x / x->cw;
		for (wheel += GET_WHEEL_DELTA_WPARAM(w); wheel >= WHEEL_DELTA && rn < RING; wheel -= WHEEL_DELTA)
			put(KEY_WHEEL_UP, x->id);
		for (; wheel <= -WHEEL_DELTA && rn < RING; wheel += WHEEL_DELTA) put(KEY_WHEEL_DOWN, x->id);
		return 0;
	}
	case WM_SIZE:
		if (w != SIZE_MINIMIZED && (LOWORD(l) != x->dw || HIWORD(l) != x->dh)) {
			x->resized = x->stale = 1;
			if (x->sizing) x->sizing = 2;
		}
		return 0;
	case WM_ENTERSIZEMOVE:
		x->sizing = 1;
		return 0;
	case WM_EXITSIZEMOVE:   /* the wallpaper smooth again */
		if (x->sizing == 2 && x->wdib) x->resized = x->stale = 1;
		x->sizing = 0;
		return 0;
	case WM_DPICHANGED: {
		const RECT *r = (const RECT *)l;
		x->dpi = HIWORD(w);
		make_font(x);
		SetWindowPos(h, NULL, r->left, r->top, r->right - r->left, r->bottom - r->top,
		             SWP_NOZORDER | SWP_NOACTIVATE);
		x->resized = x->stale = 1;
		return 0;
	}
	/* moved to another monitor (dragged, Win+Shift+Arrow), or the monitors
	   changed: its refresh period may be another; DefWindowProc then sends
	   WM_SIZE */
	case WM_WINDOWPOSCHANGED:
		if (MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST) != x->mon) measure_period(x);
		break;
	case WM_DISPLAYCHANGE:
		measure_period(x);
		break;
	/* another of ours got the keyboard (the user clicked it, Alt-Tab, SPC 5
	   o): a focus change.  The same one back from another program is none,
	   so its insert mode stays */
	case WM_SETFOCUS:
		if (x->id != focused) { focused = x->id; put(KEY_FOCUS, x->id); }
		break;
	case WM_PAINT: {
		PAINTSTRUCT ps;
		HDC dc = BeginPaint(h, &ps);
		RECT *r = &ps.rcPaint;
		BitBlt(dc, r->left, r->top, r->right - r->left, r->bottom - r->top, x->mem, r->left, r->top, SRCCOPY);
		EndPaint(h, &ps);
		return 0;
	}
	case WM_ERASEBKGND:
		return 1;
	case WM_CLOSE:   /* the editor decides (:qa), not DefWindowProc */
		put(KEY_CLOSE, x->id);
		return 0;
	}
	return DefWindowProcW(h, m, w, l);
}

static void win_free(struct win *w)
{
	if (w->mem) { SelectObject(w->mem, w->nodib); DeleteDC(w->mem); }
	if (w->wdc) { SelectObject(w->wdc, w->nowdib); DeleteDC(w->wdc); }
	if (w->wdib) DeleteObject(w->wdib);
	if (w->dib) DeleteObject(w->dib);
	if (w->h) { SetWindowLongPtrW(w->h, GWLP_USERDATA, 0); DestroyWindow(w->h); }
	free(w);
}

static void win_close(void)
{
	while (wins) {
		struct win *w = wins;
		wins = w->link;
		win_free(w);
	}
	font_close();
	if (timer) CloseHandle(timer);
	UnregisterClassW(L"homeros", GetModuleHandleW(NULL));
	timer = NULL;
	armed = 0;
	focused = -1;
}

static int win_open(void)
{
	WNDCLASSW wc = { .lpfnWndProc = proc, .hInstance = GetModuleHandleW(NULL),
	                 .hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_IBEAM), .lpszClassName = L"homeros",
	                 /* assets/homeros.rc; Windows takes the title bar's small
	                    icon from the same resource */
	                 .hIcon = LoadIconW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(1)) };
	SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
	if (!RegisterClassW(&wc)) return -1;
	timer = CreateWaitableTimerExW(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
	if (!timer) { win_close(); return -1; }
	return 0;
}

static void win_frame_close(int id)
{
	struct win **p = &wins;
	while (*p && (*p)->id != id) p = &(*p)->link;
	if (!*p) return;
	struct win *w = *p;
	*p = w->link;
	win_free(w);
}

/* A new window opens where Windows puts it: on the monitor of the window
   in front (CW_USEDEFAULT), at that monitor's DPI (Per-Monitor V2) */
static int frame_open(int id)
{
	struct win *w = calloc(1, sizeof *w);
	if (!w) return -1;
	w->id = id;
	w->link = wins;
	wins = w;
	if (focused < 0) focused = id;   /* the first window's first focus is no change */
	if (!(w->mem = CreateCompatibleDC(NULL)) || !(w->wdc = CreateCompatibleDC(NULL))) goto fail;
	w->nodib = GetCurrentObject(w->mem, OBJ_BITMAP);
	w->nowdib = GetCurrentObject(w->wdc, OBJ_BITMAP);
	if (!CreateWindowExW(0, L"homeros", L"homeros", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
	                     CW_USEDEFAULT, CW_USEDEFAULT, NULL, NULL, GetModuleHandleW(NULL), w)) goto fail;
	w->dpi = (int)GetDpiForWindow(w->h);
	if (make_font(w) || make_dib(w)) goto fail;
	measure_period(w);
	/* The title bar in the window's base and text (it was white); dark
	   mode makes the caption buttons light.  Windows 10 takes only the
	   dark mode and ignores the colours (Windows 11 22000 added them); a
	   drawn title bar would mean hit-testing and losing the snap layouts. */
	BOOL dark = TRUE;
	static const COLORREF BASE = RGB(PAINT_BASE >> 16, PAINT_BASE >> 8, PAINT_BASE), TEXT = RGB(0xcd, 0xd6, 0xf4);   /* RGB takes each byte */
	DwmSetWindowAttribute(w->h, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof dark);
	DwmSetWindowAttribute(w->h, DWMWA_CAPTION_COLOR, &BASE, sizeof BASE);
	DwmSetWindowAttribute(w->h, DWMWA_TEXT_COLOR, &TEXT, sizeof TEXT);
	/* the first window full size on its monitor (Davy, 2026-11-24); a new
	   frame at Windows' default size, so it can go to another monitor */
	ShowWindow(w->h, w->link ? SW_SHOWNORMAL : SW_SHOWMAXIMIZED);
	w->resized = w->stale = 0;   /* the size backend_size gives now is the first one */
	return 0;
fail:
	win_frame_close(id);
	return -1;
}

/* SPC 5 o: Windows lets the program in front bring its other window
   forward; the focus it gives comes inside the call (WM_SETFOCUS is sent,
   not posted), so KEY_RAISED follows it (Focus.tla) */
static void win_raise(int id)
{
	struct win *w = win_of(id);
	if (w) {
		if (IsIconic(w->h)) ShowWindow(w->h, SW_RESTORE);
		SetForegroundWindow(w->h);
	}
	put(KEY_RAISED, -1);
}

/* loop.h: a handle cut to int (its low 32 bits are all of it) */
static int win_fds(int fds[2])
{
	fds[0] = LOOP_MSGQ;
	fds[1] = (int)(INT_PTR)timer;
	return 2;
}

static int win_input(int *out, int *from, int n)
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
	for (struct win *w = wins; w && k < n; w = w->link)
		if (w->resized) { w->resized = 0; out[k] = KEY_RESIZE; from[k++] = w->id; }
	for (; k < n && rn; k++, rn--, rh = (rh + 1) % RING) { out[k] = ring[rh][0]; from[k] = ring[rh][1]; }
	return k;
}

static void win_size(int id, int *rows, int *cols)
{
	const struct win *w = win_of(id);
	RECT r = { 0 };
	if (w) GetClientRect(w->h, &r);
	*rows = w && r.bottom / w->ch > 0 ? r.bottom / w->ch : 1;
	*cols = w && r.right / w->cw > 0 ? r.right / w->cw : 1;
}

/* One timer for all windows, set for the soonest frame one waits for */
static int win_ready(int id)
{
	const struct win *w = win_of(id);
	LARGE_INTEGER now, f, due;
	if (!w || IsIconic(w->h)) return 0;
	QueryPerformanceCounter(&now);
	if (now.QuadPart >= w->next) return 1;
	/* set again on every call, for the soonest still to come: the timer
	   and the counter are different clocks, so a timer that fired a little
	   early must not leave the loop waiting forever.  Relative, in 100 ns
	   units, rounded up. */
	if (armed <= now.QuadPart || armed > w->next) armed = w->next;
	QueryPerformanceFrequency(&f);
	due.QuadPart = -(((armed - now.QuadPart) * 10000000 + f.QuadPart - 1) / f.QuadPart);
	SetWaitableTimer(timer, &due, 0, NULL, NULL, FALSE);
	return 0;
}

static void win_present(int id, const Grid *g, Grid *shown)
{
	struct win *w = win_of(id);
	if (!w || make_font(w)) return;
	int all = w->stale || shown->rows != g->rows || shown->cols != g->cols;
	if (all && (grid_resize(shown, g->rows, g->cols) || make_dib(w))) return;
	w->stale = 0;
	unsigned char dirty[g->rows];
	GdiFlush();   /* CreateDIBSection's rule: GDI done with the bits (the last BitBlt) before they change */
	paint(w->bits, w->dw, w->wbits, w->dw, g, shown, all, dirty);
	HDC dc = GetDC(w->h);
	if (all) BitBlt(dc, 0, 0, w->dw, w->dh, w->mem, 0, 0, SRCCOPY);
	else for (int r = 0; r < g->rows; r++) if (dirty[r]) BitBlt(dc, 0, r * w->ch, g->cols * w->cw, w->ch, w->mem, 0, r * w->ch, SRCCOPY);
	ReleaseDC(w->h, dc);
	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);
	w->next = now.QuadPart + w->period + w->period / 8;
}

/* gvim: "In the GUI, suspending is implemented as iconising gvim" */
static void win_suspend(void) { const struct win *w = win_of(focused); if (w) ShowWindow(w->h, SW_MINIMIZE); }

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
	if (!OpenClipboard(wins ? wins->h : NULL)) { GlobalFree(h); return -1; }
	EmptyClipboard();
	int ok = SetClipboardData(CF_UNICODETEXT, h) != NULL;
	CloseClipboard();
	if (!ok) GlobalFree(h);   /* on success the clipboard owns it */
	return ok ? 0 : -1;
}

static long win_clip_get(char *buf, size_t max)
{
	if (!OpenClipboard(wins ? wins->h : NULL)) return -1;
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
static void win_cell(int id, int *w, int *h)   /* thread 0's, agreed */
{
	const struct win *x = win_of(id);
	*w = x ? x->cw : 0;
	*h = x ? x->ch : 0;
}

/* a title longer than 63 UTF-16 units is not set (none is: "homeros [a]") */
static void win_title(int id, const char *s)
{
	struct win *w = win_of(id);
	wchar_t u[64];
	if (w && MultiByteToWideChar(CP_UTF8, 0, s, -1, u, 64)) SetWindowTextW(w->h, u);
}

const Backend backend_win = {
	win_open, win_close, frame_open, win_frame_close, win_raise, win_fds, win_input, win_size, win_ready,
	win_present, win_suspend, win_clip_set, win_clip_get, win_mouse, win_cell, win_title
};
#endif
