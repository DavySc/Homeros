/* Finding 2: what tests/test_win.c never looks at can be looked at under
   Wine: a letter leaves foreground pixels in its cell, a wide character
   covers two cells, dim text has its own colour, WM_PAINT restores the
   window from the DIB, Ctrl-H stays 8, Right and Down arrive as themselves.
   Every line here passes on the PR's code; none is in the test. */
#include <windows.h>
#include <stdio.h>
#include "backend.h"
#include "keys.h"

static HWND win;
static int cw, ch;
static const COLORREF BG = RGB(30, 30, 46), FG = RGB(205, 214, 244), DIM = RGB(108, 112, 134);

/* how many pixels of cell (r, c) have exactly colour p */
static int count(int r, int c, COLORREF p)
{
	HDC dc = GetDC(win);
	int n = 0;
	for (int y = r * ch; y < (r + 1) * ch; y++)
		for (int x = c * cw; x < (c + 1) * cw; x++) n += GetPixel(dc, x, y) == p;
	ReleaseDC(win, dc);
	return n;
}
static void present(const Grid *g, Grid *shown)
{
	while (!backend_win.ready()) Sleep(1);
	backend_win.present(g, shown);
}

int main(void)
{
	Grid g = { 0 }, shown = { 0 };
	int rows, cols, k[16];
	if (backend_win.open()) return 2;
	win = FindWindowW(L"homeros", NULL);
	while (backend_win.input(k, 16) > 0) {}
	backend_win.size(&rows, &cols);
	grid_resize(&g, rows, cols);
	g.crow = rows - 1; g.ccol = cols - 1;
	grid_put(&g, 0, 0, (const unsigned char *)" ", 1, 1, ATTR_REV);
	present(&g, &shown);
	HDC dc = GetDC(win);
	while (GetPixel(dc, cw, 0) == FG) cw++;
	while (GetPixel(dc, 0, ch) == FG) ch++;
	ReleaseDC(win, dc);

	grid_put(&g, 1, 1, (const unsigned char *)"M", 1, 1, ATTR_NONE);
	grid_put(&g, 2, 1, (const unsigned char *)"M", 1, 1, ATTR_DIM);
	grid_put(&g, 3, 1, (const unsigned char *)"\xE4\xB8\xAD", 3, 2, ATTR_REV);   /* U+4E2D, two cells */
	present(&g, &shown);
	printf("cell %dx%d = %d pixels; 'M' normal: %d exactly the text colour, %d not background; 'M' dim: %d exactly dim, %d not background\n",
	       cw, ch, cw * ch, count(1, 1, FG), cw * ch - count(1, 1, BG), count(2, 1, DIM), cw * ch - count(2, 1, BG));
	printf("wide reversed character: left cell %d reversed pixels, right cell %d, cell after it %d\n",
	       count(3, 1, FG), count(3, 2, FG), count(3, 3, FG));

	/* something paints over the window, then Windows asks for a repaint */
	dc = GetDC(win);
	RECT rc; GetClientRect(win, &rc);
	FillRect(dc, &rc, GetStockObject(WHITE_BRUSH));
	ReleaseDC(win, dc);
	InvalidateRect(win, NULL, FALSE);
	UpdateWindow(win);
	printf("after WM_PAINT: cell (5,5) background pixels %d of %d\n", count(5, 5, BG), cw * ch);

	BYTE ks[256] = { 0 };
	ks[VK_CONTROL] = 0x80;
	SetKeyboardState(ks);
	PostMessageW(win, WM_CHAR, 8, 0);
	PostMessageW(win, WM_KEYDOWN, VK_RIGHT, 0);
	PostMessageW(win, WM_KEYDOWN, VK_DOWN, 0);
	PostMessageW(win, WM_CHAR, 0xDCDD, 0);   /* a low surrogate alone: no key */
	int n = 0, r;
	while ((r = backend_win.input(k + n, 16 - n)) > 0) n += r;
	printf("Ctrl-H, Right, Down, lone low surrogate: %d keys: %d %s %s\n", n, k[0],
	       k[1] == KEY_RIGHT ? "KEY_RIGHT" : "?", k[2] == KEY_DOWN ? "KEY_DOWN" : "?");
	backend_win.close();
	return 0;
}
