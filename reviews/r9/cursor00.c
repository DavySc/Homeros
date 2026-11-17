/* Finding 1: after a whole redraw (first frame, new size, after Ctrl-Z) the
   cursor is not drawn when it is at row 0, column 0 on an empty cell. */
#include <windows.h>
#include <stdio.h>
#include "backend.h"

static HWND win;
static COLORREF px(int x, int y)
{
	HDC dc = GetDC(win);
	COLORREF p = GetPixel(dc, x, y);
	ReleaseDC(win, dc);
	return p;
}
static void present(const Grid *g, Grid *shown)
{
	while (!backend_win.ready()) Sleep(1);
	backend_win.present(g, shown);
}
static const char *name(COLORREF p)
{
	return p == RGB(205, 214, 244) ? "reversed (cursor)" : p == RGB(30, 30, 46) ? "background (no cursor)" : "other";
}

int main(void)
{
	Grid g = { 0 }, shown = { 0 };
	int rows, cols, bad = 0;
	if (backend_win.open()) return 2;
	win = FindWindowW(L"homeros", NULL);
	backend_win.size(&rows, &cols);
	if (grid_resize(&g, rows, cols)) return 2;   /* all spaces, cursor at (0, 0) */

	present(&g, &shown);
	printf("first frame, cursor (0,0) on a space:        %s\n", name(px(2, 2)));
	bad += px(2, 2) != RGB(205, 214, 244);

	g.crow = 1; g.ccol = 1;
	present(&g, &shown);
	g.crow = 0; g.ccol = 0;
	present(&g, &shown);
	printf("control: moved away and back to (0,0):       %s\n", name(px(2, 2)));

	grid_free(&shown);   /* what src/main.c does after Ctrl-Z */
	present(&g, &shown);
	printf("after Ctrl-Z's whole redraw, cursor (0,0):   %s\n", name(px(2, 2)));
	bad += px(2, 2) != RGB(205, 214, 244);

	g.crow = 1; g.ccol = 1;
	present(&g, &shown);
	grid_free(&shown);
	present(&g, &shown);
	printf("control: whole redraw with cursor at (1,1):  %s at (1,1)\n", name(px(12, 24)));
	backend_win.close();
	return bad ? 1 : 0;
}
