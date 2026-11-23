/* PR 135, after review: where the wallpaper's per-cell cost goes, and what
   a new size costs with the source picture kept.  Variants over a 128 x 36
   grid of Consolas cells, alternating, RUNS each; medians and ranges.
   Build: zig cc -target x86_64-windows-gnu -O2 gdi.c -lgdi32 -luser32 */
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { RUNS = 9, COLS = 128, ROWS = 36, V = 7 };
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
	printf("%-62s median %8.3f ms (%.3f-%.3f)\n", what, t[RUNS / 2], t[0], t[RUNS - 1]);
}
static HBITMAP dibsec(HDC dc, int w, int h, void **px)
{
	BITMAPINFO bi = { .bmiHeader = { .biSize = sizeof bi.bmiHeader, .biWidth = w, .biHeight = -h, .biPlanes = 1, .biBitCount = 32 } };
	return CreateDIBSection(dc, &bi, DIB_RGB_COLORS, px, NULL, 0);
}
static const char *name[V] = {
	"A per cell: opaque text (no wallpaper)",
	"B per cell: BitBlt + transparent text (PR 135 head)",
	"C per cell: transparent text alone",
	"D per cell: BitBlt alone",
	"E per row: one BitBlt, then transparent text per cell",
	"F per cell: opaque text, mode set only when it changes",
	"G per cell: BitBlt + transparent, mode set only when it changes",
};

int main(void)
{
	HDC mem = CreateCompatibleDC(NULL), wdc = CreateCompatibleDC(NULL), sdc = CreateCompatibleDC(NULL);
	double t[V][RUNS];
	void *px;
	int W = 1280, H = 800;
	HBITMAP d = dibsec(mem, W, H, &px), wb = dibsec(mem, W, H, &px);
	for (int i = 0; i < W * H; i++) ((uint32_t *)px)[i] = (uint32_t)(i * 2654435761u) >> 8;
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
	for (int r = 0; r < RUNS; r++)
		for (int v = 0; v < V; v++) {
			int mode = -1;
			double a = now();
			for (int k = 0; k < 10; k++)
				for (int y = 0; y < ROWS; y++) {
					if (v == 4) BitBlt(mem, 0, y * ch, COLS * cw, ch, wdc, 0, y * ch, SRCCOPY);
					for (int x = 0; x < COLS; x++) {
						RECT rc = { x * cw, y * ch, (x + 1) * cw, (y + 1) * ch };
						WCHAR c = (WCHAR)(L'a' + (x + y + k) % 26);
						int blt = v == 1 || v == 3 || v == 6, tr = v == 1 || v == 2 || v == 4 || v == 6;
						if (blt) BitBlt(mem, rc.left, rc.top, cw, ch, wdc, rc.left, rc.top, SRCCOPY);
						if (v == 3) continue;
						if (v < 5) SetBkMode(mem, tr ? TRANSPARENT : OPAQUE);
						else if (mode != tr) SetBkMode(mem, (mode = tr) ? TRANSPARENT : OPAQUE);
						ExtTextOutW(mem, rc.left, rc.top, (tr ? 0 : ETO_OPAQUE) | ETO_CLIPPED, &rc, &c, 1, NULL);
					}
				}
			GdiFlush();
			t[v][r] = (now() - a) / 10;
		}
	printf("cell %dx%d, %dx%d cells\n", cw, ch, COLS, ROWS);
	for (int v = 0; v < V; v++) report(name[v], t[v]);

	/* a new size with the source picture kept in its DIB (made once per picture) */
	static const int win[4][2] = { { 1280, 800 }, { 1920, 1080 }, { 2560, 1440 }, { 3840, 2160 } };
	int iw = 1920, ih = 1080;
	HBITMAP sb = dibsec(mem, iw, ih, &px);
	for (int i = 0; i < iw * ih; i++) ((uint32_t *)px)[i] = (uint32_t)(i * 2654435761u) >> 8;
	SelectObject(sdc, sb);
	char what[128];
	for (int s = 0; s < 4; s++)
		for (int m = 0; m < 2; m++) {
			double u[RUNS];
			for (int r = 0; r < RUNS; r++) {
				double a = now();
				HBITMAP b = dibsec(mem, win[s][0], win[s][1], NULL);
				HGDIOBJ o = SelectObject(wdc, b);
				SetStretchBltMode(wdc, m ? COLORONCOLOR : HALFTONE);
				SetBrushOrgEx(wdc, 0, 0, NULL);
				StretchBlt(wdc, 0, 0, win[s][0], win[s][1], sdc, 0, 0, iw, ih, SRCCOPY);
				GdiFlush();
				u[r] = now() - a;
				SelectObject(wdc, o);
				DeleteObject(b);
			}
			snprintf(what, sizeof what, "new size, source kept: 1920x1080 to %dx%d, %s", win[s][0], win[s][1], m ? "COLORONCOLOR" : "HALFTONE");
			report(what, u);
			/* StretchDIBits straight from the pixels: no source bitmap at all */
			BITMAPINFO bi = { .bmiHeader = { .biSize = sizeof bi.bmiHeader, .biWidth = iw, .biHeight = -ih, .biPlanes = 1, .biBitCount = 32 } };
			for (int r = 0; r < RUNS; r++) {
				double a = now();
				HBITMAP b = dibsec(mem, win[s][0], win[s][1], NULL);
				HGDIOBJ o = SelectObject(wdc, b);
				SetStretchBltMode(wdc, m ? COLORONCOLOR : HALFTONE);
				SetBrushOrgEx(wdc, 0, 0, NULL);
				StretchDIBits(wdc, 0, 0, win[s][0], win[s][1], 0, 0, iw, ih, px, &bi, DIB_RGB_COLORS, SRCCOPY);
				GdiFlush();
				u[r] = now() - a;
				SelectObject(wdc, o);
				DeleteObject(b);
			}
			snprintf(what, sizeof what, "new size, StretchDIBits: 1920x1080 to %dx%d, %s", win[s][0], win[s][1], m ? "COLORONCOLOR" : "HALFTONE");
			report(what, u);
		}
	/* StretchDIBits' source origin for a top-down DIB: 4 rows, 1 to 3 of
	   them asked for; the top row drawn says which row it took */
	{
		uint32_t four[4] = { 0x000001, 0x000002, 0x000003, 0x000004 };
		BITMAPINFO bi = { .bmiHeader = { .biSize = sizeof bi.bmiHeader, .biWidth = 1, .biHeight = -4, .biPlanes = 1, .biBitCount = 32 } };
		void *q;
		HBITMAP b = dibsec(mem, 1, 3, &q);
		HGDIOBJ o = SelectObject(wdc, b);
		for (int m = 0; m < 2; m++) {
			SetStretchBltMode(wdc, m ? COLORONCOLOR : HALFTONE);
			StretchDIBits(wdc, 0, 0, 1, 3, 0, 1, 1, 3, four, &bi, DIB_RGB_COLORS, SRCCOPY);
			GdiFlush();
			printf("StretchDIBits rows 1-3 of a top-down DIB, %s: drew %u %u %u (2 3 4: counted from the top)\n", m ? "COLORONCOLOR" : "HALFTONE",
			       ((uint32_t *)q)[0] & 0xff, ((uint32_t *)q)[1] & 0xff, ((uint32_t *)q)[2] & 0xff);
		}
		SelectObject(wdc, o);
		DeleteObject(b);
	}
	return 0;
}
