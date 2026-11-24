/* What the wallpaper costs GDI (PR 135's review): the calls src/win/backend.c
   makes, timed.  (a) at a new window size: the memcpy of the picture into a
   DIB and StretchBlt HALFTONE to the window's size, against COLORONCOLOR;
   (b) per cell: ExtTextOut opaque (no wallpaper) against BitBlt of the cell
   from the wallpaper's bitmap, then ExtTextOut transparent (with one), over
   a 128 x 36 grid; (c) SetBkMode alone, which every cell now calls.
   Medians of RUNS runs, the two sides of (b) alternating.
   Build: zig cc -target x86_64-windows-gnu -O2 gdiprobe.c -lgdi32 -luser32 */
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { RUNS = 9, COLS = 128, ROWS = 36 };
static double now(void)
{
	LARGE_INTEGER t, f;
	QueryPerformanceCounter(&t);
	QueryPerformanceFrequency(&f);
	return (double)t.QuadPart * 1e3 / (double)f.QuadPart;
}
static int cmp(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y); }
static void report(const char *what, double *t)
{
	qsort(t, RUNS, sizeof *t, cmp);
	printf("%-58s median %9.3f ms (%.3f-%.3f)\n", what, t[RUNS / 2], t[0], t[RUNS - 1]);
}
static HBITMAP dibsec(HDC dc, int w, int h, void **px)
{
	BITMAPINFO bi = { .bmiHeader = { .biSize = sizeof bi.bmiHeader, .biWidth = w, .biHeight = -h, .biPlanes = 1, .biBitCount = 32 } };
	return CreateDIBSection(dc, &bi, DIB_RGB_COLORS, px, NULL, 0);
}

int main(void)
{
	static const int pic[2][2] = { { 1920, 1080 }, { 3840, 2160 } }, win[4][2] = { { 1280, 800 }, { 1920, 1080 }, { 2560, 1440 }, { 3840, 2160 } };
	HDC mem = CreateCompatibleDC(NULL), wdc = CreateCompatibleDC(NULL), sdc = CreateCompatibleDC(NULL);
	double t[RUNS], u[RUNS];
	char what[128];
	for (int p = 0; p < 2; p++) {
		int iw = pic[p][0], ih = pic[p][1];
		uint32_t *src = malloc((size_t)iw * (size_t)ih * 4);
		for (int i = 0; i < iw * ih; i++) src[i] = (uint32_t)(i * 2654435761u) >> 8;
		for (int s = 0; s < 4; s++)
			for (int mode = 0; mode < 2; mode++) {
				for (int r = 0; r < RUNS; r++) {
					void *px;
					double a = now();
					HBITMAP sb = dibsec(mem, iw, ih, &px), wb = dibsec(mem, win[s][0], win[s][1], NULL);
					memcpy(px, src, (size_t)iw * (size_t)ih * 4);
					HGDIOBJ o = SelectObject(sdc, sb), o2 = SelectObject(wdc, wb);
					SetStretchBltMode(wdc, mode ? COLORONCOLOR : HALFTONE);
					SetBrushOrgEx(wdc, 0, 0, NULL);
					StretchBlt(wdc, 0, 0, win[s][0], win[s][1], sdc, 0, 0, iw, ih, SRCCOPY);
					GdiFlush();
					t[r] = now() - a;
					SelectObject(sdc, o);
					SelectObject(wdc, o2);
					DeleteObject(sb);
					DeleteObject(wb);
				}
				snprintf(what, sizeof what, "new size: %dx%d picture to %dx%d, %s", iw, ih, win[s][0], win[s][1], mode ? "COLORONCOLOR" : "HALFTONE");
				report(what, t);
			}
		free(src);
	}
	/* per cell, as draw_cell: 12 pt at 96 dpi */
	void *px;
	int W = 1280, H = 800;
	HBITMAP d = dibsec(mem, W, H, &px), wb = dibsec(mem, W, H, &px);
	SelectObject(mem, d);
	SelectObject(wdc, wb);
	HFONT f = CreateFontW(-16, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
	                      ANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
	SelectObject(mem, f);
	TEXTMETRICW tm;
	GetTextMetricsW(mem, &tm);
	int cw = tm.tmAveCharWidth, ch = tm.tmHeight;
	SetTextColor(mem, RGB(0xcd, 0xd6, 0xf4));
	SetBkColor(mem, RGB(0x1e, 0x1e, 0x2e));
	for (int r = 0; r < 2 * RUNS; r++) {
		int wall = r & 1;
		double a = now();
		for (int k = 0; k < 10; k++)
			for (int y = 0; y < ROWS; y++)
				for (int x = 0; x < COLS; x++) {
					RECT rc = { x * cw, y * ch, (x + 1) * cw, (y + 1) * ch };
					WCHAR c = (WCHAR)(L'a' + (x + y + k) % 26);
					int back = wall && BitBlt(mem, rc.left, rc.top, cw, ch, wdc, rc.left, rc.top, SRCCOPY);
					SetBkMode(mem, back ? TRANSPARENT : OPAQUE);
					ExtTextOutW(mem, rc.left, rc.top, (back ? 0 : ETO_OPAQUE) | ETO_CLIPPED, &rc, &c, 1, NULL);
				}
		GdiFlush();
		(wall ? u : t)[r / 2] = (now() - a) / 10;
	}
	printf("cell %dx%d, %dx%d cells\n", cw, ch, COLS, ROWS);
	report("whole grid, opaque cells (no wallpaper)", t);
	report("whole grid, BitBlt + transparent text (wallpaper)", u);
	for (int r = 0; r < 2 * RUNS; r++) {
		int with = r & 1;
		double a = now();
		for (int i = 0; i < COLS * ROWS * 10; i++) {
			if (with) SetBkMode(mem, OPAQUE);
			SetBkColor(mem, RGB(0x1e, 0x1e, 0x2e + (i & 1)));
		}
		(with ? u : t)[r / 2] = (now() - a) / 10;
	}
	report("whole grid of SetBkColor alone", t);
	report("whole grid of SetBkColor + SetBkMode", u);
	return 0;
}
