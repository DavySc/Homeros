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
#include "image.h"
#include "keys.h"
#include "loop.h"
#include "uni.h"
#include <windowsx.h>

enum { RING = 256, PT = 12 };   /* font size in points: a guess until package 7 */

/* A window per tab page, each with its monitor's DPI (so its own font)
   and refresh period (so its own pacing) */
static struct win {
	int id;                 /* its tab page (backend.h's id) */
	HWND h;
	HMONITOR mon;           /* the monitor its period was measured on */
	HDC mem;                /* holds the DIB, which holds what is shown */
	HBITMAP dib, nodib;
	uint32_t *bits;         /* the DIB's pixels, for images */
	HFONT font, nofont;
	int cw, ch, dpi;        /* cell size in pixels */
	int dw, dh, stale;      /* the DIB's size; stale: redraw it whole */
	int resized;
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

/* Catppuccin Mocha (FEATURES.md): the window's own text, dim (overlay0)
   and base, which a terminal takes from its settings; the faces' colours
   come from grid.h's faces[].  Their styles (bold, italic, underline,
   strike) wait for package 7's fonts. */
static const COLORREF TEXT = RGB(205, 214, 244), DIMC = RGB(108, 112, 134), BASE = RGB(30, 30, 46);

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

/* The first installed of JetBrains Mono's Nerd Font builds (DESIGN.md), for
   the icons, then plain JetBrains Mono: GDI substitutes any font for a
   missing family, so the face it gave is compared with the one asked.
   Greyscale smoothing: ClearType's coloured fringes made text look blue on
   the dark background on Davy's laptop. */
static int make_font(struct win *w)
{
	static const WCHAR *const family[] = { L"JetBrainsMono Nerd Font Mono", L"JetBrainsMono NFM",
	                                       L"JetBrainsMono Nerd Font", L"JetBrains Mono" };
	HFONT f = NULL;
	for (unsigned i = 0; i < sizeof family / sizeof *family; i++) {
		WCHAR face[LF_FACESIZE];
		if (f) DeleteObject(f);
		f = CreateFontW(-MulDiv(PT, w->dpi, 72), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
		                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
		                FIXED_PITCH | FF_MODERN, family[i]);
		if (!f) return -1;
		HGDIOBJ old = SelectObject(w->mem, f);
		GetTextFaceW(w->mem, LF_FACESIZE, face);
		SelectObject(w->mem, old);
		if (!lstrcmpiW(face, family[i])) break;
	}
	TEXTMETRICW tm;
	SelectObject(w->mem, f);
	if (w->font) DeleteObject(w->font);
	w->font = f;
	GetTextMetricsW(w->mem, &tm);
	w->cw = tm.tmAveCharWidth;
	w->ch = tm.tmHeight;
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
	SetBkColor(w->mem, BASE);
	ExtTextOutW(w->mem, 0, 0, ETO_OPAQUE, &r, NULL, 0, NULL);
	w->dw = r.right;
	w->dh = r.bottom;
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
	case WM_KEYDOWN:
		switch (w) {
		case VK_LEFT: put(KEY_LEFT, x->id); return 0;
		case VK_RIGHT: put(KEY_RIGHT, x->id); return 0;
		case VK_UP: put(KEY_UP, x->id); return 0;
		case VK_DOWN: put(KEY_DOWN, x->id); return 0;
		case VK_DELETE: put(KEY_DEL, x->id); return 0;
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
		if (w != SIZE_MINIMIZED && (LOWORD(l) != x->dw || HIWORD(l) != x->dh)) x->resized = x->stale = 1;
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
	if (w->mem) { SelectObject(w->mem, w->nofont); SelectObject(w->mem, w->nodib); DeleteDC(w->mem); }
	if (w->font) DeleteObject(w->font);
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
	if (!(w->mem = CreateCompatibleDC(NULL))) goto fail;
	w->nofont = SelectObject(w->mem, GetStockObject(SYSTEM_FONT));
	w->nodib = GetCurrentObject(w->mem, OBJ_BITMAP);
	if (!CreateWindowExW(0, L"homeros", L"homeros", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
	                     CW_USEDEFAULT, CW_USEDEFAULT, NULL, NULL, GetModuleHandleW(NULL), w)) goto fail;
	w->dpi = (int)GetDpiForWindow(w->h);
	if (make_font(w) || make_dib(w)) goto fail;
	measure_period(w);
	ShowWindow(w->h, SW_SHOWDEFAULT);
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

static void draw_cell(struct win *w, const Grid *g, int r, int c, int cursor)
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
	const Face *fb, *fu, *fa = grid_face(x, &fb, &fu);
	COLORREF f = x->attr == ATTR_DIM ? DIMC : fa->style & FACE_FG ? RGB(fa->fg[0], fa->fg[1], fa->fg[2]) : TEXT;
	COLORREF b = fb->style & FACE_BG ? RGB(fb->bg[0], fb->bg[1], fb->bg[2]) : BASE;
	RECT rc = { c * w->cw, r * w->ch, (c + 1 + wide) * w->cw, (r + 1) * w->ch };
	if (x->attr == ATTR_IMAGE) {   /* thread 0's, agreed: the background, then the image over it */
		SetBkColor(w->mem, b);
		ExtTextOutW(w->mem, 0, 0, ETO_OPAQUE, &rc, NULL, 0, NULL);
		GdiFlush();   /* CreateDIBSection's rule before touching the bits (Wine draws at once, so no test sees it) */
		image_draw(w->bits + r * w->ch * w->dw + c * w->cw, w->dw, x);
		return;
	}
	/* reversed (status line, cursor): the colours swap, as a terminal's SGR 7 */
	int rev = cursor || x->attr == ATTR_REV;
	SetTextColor(w->mem, rev ? b : f);
	SetBkColor(w->mem, rev ? f : b);
	ExtTextOutW(w->mem, rc.left, rc.top, ETO_OPAQUE | ETO_CLIPPED, &rc, u, n, NULL);
	/* an underline or undercurl in sp, else the text's colour (Neovim's GUIs) */
	if (!(fu->style & (FACE_UNDERLINE | FACE_UNDERCURL))) return;
	SetBkColor(w->mem, fu->style & FACE_SP ? RGB(fu->sp[0], fu->sp[1], fu->sp[2]) : f);
	int curl = fu->style & FACE_UNDERCURL;
	for (int i = rc.left; i < rc.right; i = curl ? i + 1 : rc.right) {   /* a curl pixel by pixel, a line at once */
		int y = rc.bottom - 1 - (curl ? grid_curl(i, w->ch) : 0);
		RECT p = { i, y, curl ? i + 1 : rc.right, y + 1 };
		ExtTextOutW(w->mem, 0, 0, ETO_OPAQUE, &p, NULL, 0, NULL);
	}
}

static void win_present(int id, const Grid *g, Grid *shown)
{
	struct win *w = win_of(id);
	if (!w) return;
	int all = w->stale || shown->rows != g->rows || shown->cols != g->cols;
	if (all && (grid_resize(shown, g->rows, g->cols) || make_dib(w))) return;
	w->stale = 0;
	HDC dc = GetDC(w->h);
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
			draw_cell(w, g, r, c, cur);
			if (c < lo) lo = c;
			hi = c + 1 + (c + 1 < g->cols && !x[1].len);
		}
		if (hi && !all) BitBlt(dc, lo * w->cw, r * w->ch, (hi - lo) * w->cw, w->ch, w->mem, lo * w->cw, r * w->ch, SRCCOPY);
	}
	if (all) BitBlt(dc, 0, 0, w->dw, w->dh, w->mem, 0, 0, SRCCOPY);
	ReleaseDC(w->h, dc);
	shown->crow = g->crow;
	shown->ccol = g->ccol;
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

const Backend backend_win = {
	win_open, win_close, frame_open, win_frame_close, win_raise, win_fds, win_input, win_size, win_ready,
	win_present, win_suspend, win_clip_set, win_clip_get, win_mouse, win_cell
};
#endif
