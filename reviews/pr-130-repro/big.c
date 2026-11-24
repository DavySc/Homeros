/* A batch larger than the socket's buffer, N times: is it ever left half
   sent with no wake coming?  big.exe MEGABYTES TIMES
   The loop is tests/test_sql.c's run() for Windows with a 10 s limit. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "../src/sql/tds.h"
int main(int argc, char **argv)
{
	size_t mb = (size_t)atoi(argv[1]) << 20;
	int times = atoi(argv[2]), stalls = 0;
	char *sql = malloc(mb + 64);
	memcpy(sql, "/*", 2), memset(sql + 2, 'x', mb), strcpy(sql + 2 + mb, "*/ select 1 a");
	Tds t;
	if (tds_open(&t, "127.0.0.1", 1433, "sa", getenv("SQL_SA_PASSWORD"), "master", 1)) return puts(t.net.err), 1;
	int sent = 0;
	size_t wakes = 0, blocked = 0;
	for (int i = 0; i < times;) {
		if (t.state == TDS_READY && !sent) { t.out.n = 0; if (tds_batch(&t, sql)) return puts("batch"), 1; sent = 1; blocked += tds_pending(&t); continue; }
		if (t.state == TDS_READY && sent) { sent = 0, i++; continue; }
		DWORD w = WaitForSingleObject((HANDLE)(INT_PTR)t.net.fd, 10000);
		if (w == WAIT_TIMEOUT) {
			printf("STALL in batch %d: %zu bytes still queued, no wake for 10 s\n", i, t.net.snd.n);
			stalls++;
			break;
		}
		wakes++;
		if (tds_io(&t, 1) < 0) return printf("dead: %s\n", t.net.err), 1;
	}
	printf("big: %d stalls; %zu wakes, %zu batches left bytes queued after the first send\n", stalls, wakes, blocked);
	return stalls ? 2 : 0;
}
