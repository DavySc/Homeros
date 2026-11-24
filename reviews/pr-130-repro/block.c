/* How long the editor's one thread is held by tds_open (name lookup and,
   for the Windows login, SSPI's first token) and by the longest tds_io. */
#include <stdio.h>
#include <windows.h>
#include "tds.h"
static double ms(LARGE_INTEGER a) { LARGE_INTEGER b, f; QueryPerformanceCounter(&b), QueryPerformanceFrequency(&f); return (double)(b.QuadPart - a.QuadPart) * 1000 / (double)f.QuadPart; }
int main(int argc, char **argv)
{
	Tds t;
	LARGE_INTEGER a;
	QueryPerformanceCounter(&a);
	int r = tds_open(&t, argv[1], 1433, argc > 2 ? "sa" : NULL, argc > 2 ? argv[2] : NULL, "master", 1);
	double open = ms(a), worst = 0;
	while (!r && t.state != TDS_READY && t.state != TDS_DEAD) {
		if (WaitForSingleObject((HANDLE)(INT_PTR)t.net.fd, 20000) == WAIT_TIMEOUT) { puts("no answer for 20 s"); return 1; }
		QueryPerformanceCounter(&a);
		tds_io(&t, 1);
		if (ms(a) > worst) worst = ms(a);
	}
	printf("block: tds_open %.1f ms, longest tds_io %.1f ms, %s %s\n", open, worst, t.state == TDS_READY ? "logged in" : "FAILED:", t.state == TDS_READY ? "" : t.net.err);
	tds_close(&t);
	return 0;
}
