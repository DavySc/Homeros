/* Build: zig cc -target x86_64-windows-gnu -O2 -o dwmprobe.exe dwmprobe.c -luser32 -lgdi32 -ldwmapi
   Measures what the Win32 backend's frame pacing assumes (DESIGN.md, Pace.tla):
   DWM composes every refresh period P, each composition at most D off from P
   after the one before, with D under P/8.  For 20 s it keeps a small window
   changing (so DWM has something to compose) and records each composition's
   time from DwmGetCompositionTimingInfo.  Prints P, the gaps' median and
   range, how many were off by more than P/8, and how many were skipped. */
#include <windows.h>
#include <dwmapi.h>
#include <stdio.h>
#include <stdlib.h>

static int cmp(const void *a, const void *b) { LONGLONG x = *(const LONGLONG *)a, y = *(const LONGLONG *)b; return x < y ? -1 : x > y; }

int main(void)
{
	WNDCLASSW wc = { .lpfnWndProc = DefWindowProcW, .hInstance = GetModuleHandleW(NULL), .lpszClassName = L"dwmprobe" };
	RegisterClassW(&wc);
	HWND w = CreateWindowExW(0, L"dwmprobe", L"dwmprobe (20 s)", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100, 300, 120, NULL, NULL, wc.hInstance, NULL);
	LARGE_INTEGER f, now, end;
	QueryPerformanceFrequency(&f);
	QueryPerformanceCounter(&now);
	end.QuadPart = now.QuadPart + 20 * f.QuadPart;
	static LONGLONG at[20000], gap[20000];
	int n = 0;
	ULONGLONG last = 0;
	DWM_TIMING_INFO t = { .cbSize = sizeof t };
	LONGLONG p = 0;
	for (int i = 0; now.QuadPart < end.QuadPart && n < 20000; i++) {
		MSG m;
		while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) DispatchMessageW(&m);
		HDC dc = GetDC(w);
		RECT r = { 0, 0, 300, 120 };
		HBRUSH b = CreateSolidBrush(RGB(i & 255, 0, 0));
		FillRect(dc, &r, b);
		DeleteObject(b);
		ReleaseDC(w, dc);
		if (SUCCEEDED(DwmGetCompositionTimingInfo(NULL, &t)) && t.cFrame != last) {
			if (last) at[n++] = (LONGLONG)t.qpcCompose;
			last = t.cFrame;
			p = (LONGLONG)t.qpcRefreshPeriod;
		}
		Sleep(1);
		QueryPerformanceCounter(&now);
	}
	if (n < 3 || !p) { printf("dwmprobe: no composition timing (%d samples)\n", n); return 1; }
	int g = 0, off = 0, skipped = 0;
	for (int i = 1; i < n; i++) {
		LONGLONG d = at[i] - at[i - 1];
		if (d <= 0) continue;
		if (d > p * 3 / 2) { skipped++; continue; }   /* a refresh with no composition */
		gap[g++] = d;
		if (llabs(d - p) > p / 8) off++;
	}
	qsort(gap, (size_t)g, sizeof *gap, cmp);
	double ms = 1000.0 / (double)f.QuadPart;
	printf("dwmprobe: P %.3f ms; %d gaps: median %.3f ms, range %.3f to %.3f ms; %d off by more than P/8 (%.3f ms); %d skipped refreshes\n",
	       p * ms, g, gap[g / 2] * ms, gap[0] * ms, gap[g - 1] * ms, off, p / 8 * ms, skipped);
	return 0;
}
