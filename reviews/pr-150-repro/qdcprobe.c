/* PR 150 review probe, real Windows: each active display path's refresh
   rate from QueryDisplayConfig, next to DWM's period (the primary's) */
#include <windows.h>
#include <dwmapi.h>
#include <stdio.h>
#include <stdlib.h>
int main(void)
{
	UINT32 np, nm;
	DWM_TIMING_INFO t = { .cbSize = sizeof t };
	LARGE_INTEGER f;
	QueryPerformanceFrequency(&f);
	HRESULT h = DwmGetCompositionTimingInfo(NULL, &t);
	printf("qdcprobe: DWM %s, period %.3f ms\n", SUCCEEDED(h) ? "ok" : "failed",
	       SUCCEEDED(h) ? (double)t.qpcRefreshPeriod * 1000 / (double)f.QuadPart : 0);
	if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &np, &nm)) { puts("qdcprobe: no buffer sizes"); return 0; }
	DISPLAYCONFIG_PATH_INFO *p = malloc(np * sizeof *p);
	DISPLAYCONFIG_MODE_INFO *m = malloc(nm * sizeof *m);
	LONG r = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &np, p, &nm, m, NULL);
	printf("qdcprobe: QueryDisplayConfig %ld, %u paths\n", r, np);
	for (UINT32 i = 0; !r && i < np; i++) {
		DISPLAYCONFIG_SOURCE_DEVICE_NAME s = { .header = { DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME, sizeof s,
		                                       p[i].sourceInfo.adapterId, p[i].sourceInfo.id } };
		LONG d = DisplayConfigGetDeviceInfo(&s.header);
		DISPLAYCONFIG_RATIONAL q = p[i].targetInfo.refreshRate;
		printf("qdcprobe: path %u: %ls (%ld), refresh %u/%u = %.3f Hz\n", i, d ? L"?" : s.viewGdiDeviceName, d,
		       q.Numerator, q.Denominator, q.Denominator ? (double)q.Numerator / q.Denominator : 0);
	}
	MONITORINFOEXW mi = { .cbSize = sizeof mi };
	GetMonitorInfoW(MonitorFromPoint((POINT){ 0, 0 }, MONITOR_DEFAULTTOPRIMARY), (MONITORINFO *)&mi);
	printf("qdcprobe: primary monitor %ls\n", mi.szDevice);
	return 0;
}
