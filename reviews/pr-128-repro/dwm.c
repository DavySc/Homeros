/* What DwmSetWindowAttribute answers for PR 128's three attributes, and for
   one no Windows has (a stand-in for an older DWM that does not know them). */
#include <windows.h>
#include <dwmapi.h>
#include <stdio.h>
int main(void)
{
	HWND w = CreateWindowExW(0, L"STATIC", L"probe", WS_OVERLAPPEDWINDOW, 0, 0, 300, 200, NULL, NULL, NULL, NULL);
	BOOL dark = TRUE;
	COLORREF c = RGB(30, 30, 46);
	printf("window %s\n", w ? "made" : "NOT made");
	printf("dark mode (%d): %08lx\n", DWMWA_USE_IMMERSIVE_DARK_MODE, (unsigned long)DwmSetWindowAttribute(w, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof dark));
	printf("caption (%d): %08lx\n", DWMWA_CAPTION_COLOR, (unsigned long)DwmSetWindowAttribute(w, DWMWA_CAPTION_COLOR, &c, sizeof c));
	printf("text (%d): %08lx\n", DWMWA_TEXT_COLOR, (unsigned long)DwmSetWindowAttribute(w, DWMWA_TEXT_COLOR, &c, sizeof c));
	printf("unknown (999): %08lx\n", (unsigned long)DwmSetWindowAttribute(w, 999, &c, sizeof c));
	printf("no window: %08lx\n", (unsigned long)DwmSetWindowAttribute(NULL, DWMWA_CAPTION_COLOR, &c, sizeof c));
	ShowWindow(w, SW_SHOW);
	printf("still running after all five calls\n");
	return 0;
}
