/* Re-review of PR 140: what a new window size costs over a wallpaper on
   Windows itself, outside a drag (HALFTONE) and inside one (between
   WM_ENTERSIZEMOVE and WM_EXITSIZEMOVE the backend scales with
   COLORONCOLOR), and the frame after the drag ends (smooth again).
   Prints each present's time.  Build: winprobe.sh.  Run: sizeprobe.exe [config folder holding homeros\\wallpaper] */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "backend.h"

static const Backend *b = &backend_win;
static Grid g, shown;
static LARGE_INTEGER f;

static double frame(void)
{
	static int k[1024];
	int rows, cols;
	LARGE_INTEGER t0, t1;
	b->input(k, 1024);
	b->size(&rows, &cols);
	grid_resize(&g, rows, cols);
	for (int r = 0; r < rows; r++) grid_puts(&g, r, 0, cols, "static int pick(unsigned a) { return a; }", 41, ATTR_NONE, FACE_NONE);
	QueryPerformanceCounter(&t0);
	b->present(&g, &shown);
	QueryPerformanceCounter(&t1);
	return (double)(t1.QuadPart - t0.QuadPart) * 1e6 / (double)f.QuadPart;
}

int main(int argc, char **argv)
{
	static char env[600];
	static const int size[][2] = { { 640, 480 }, { 800, 600 }, { 1000, 700 }, { 660, 500 }, { 820, 620 }, { 980, 680 } }   /* inside the runner's 1024 x 768 screen */;
	setvbuf(stdout, NULL, _IONBF, 0);
	QueryPerformanceFrequency(&f);
	if (argc > 1) { snprintf(env, sizeof env, "XDG_CONFIG_HOME=%s", argv[1]); _putenv(env); }
	if (b->open()) return 1;
	HWND w = FindWindowW(L"homeros", NULL);
	for (int drag = 0; drag < 2; drag++)
		for (int round = 0; round < 5; round++) {
			if (drag) SendMessageW(w, WM_ENTERSIZEMOVE, 0, 0);
			for (int i = 0; i < 6; i++) {
				SetWindowPos(w, NULL, 0, 0, size[i][0], size[i][1], SWP_NOMOVE | SWP_NOZORDER);
				double us = frame();
				printf("%s %d x %d (%d x %d cells): %.0f us\n", drag ? "dragging" : "one size", size[i][0], size[i][1], g.cols, g.rows, us);
			}
			if (drag) {
				SendMessageW(w, WM_EXITSIZEMOVE, 0, 0);
				double a = frame(), z = frame();
				printf("let go at %d x %d: %.0f us, then an unchanged frame %.0f us\n", size[5][0], size[5][1], a, z);
			}
		}
	b->close();
	return 0;
}
