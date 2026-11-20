/* How long the editor's loop stands still for Lean's diagnostics as the
   bridge relays them (infoview.c included whole; built -O2 by
   bench_tap.sh): N incremental publications of one 300-byte diagnostic
   each to one file, with P pages subscribed, each publication re-sent
   whole to each page.  Prints the total and the slowest publication. */
#include <time.h>
#include "../../src/infoview/infoview.c"

static double us(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1e6 + t.tv_nsec / 1e3;
}

int main(int argc, char **argv)
{
	int n = argc > 1 ? atoi(argv[1]) : 1000, np = argc > 2 ? atoi(argv[2]) : 1;
	for (int i = 0; i < np; i++) {
		char id[16], c[200];
		snprintf(id, sizeof id, "p%d", i);
		int k = snprintf(c, sizeof c, "{\"id\":0,\"m\":\"subscribeServerNotifications\",\"a\":[\"textDocument/publishDiagnostics\"]}");
		call(id, c, (size_t)k);
	}
	char msg[1024], text[240];
	memset(text, 'm', sizeof text - 1);
	text[sizeof text - 1] = 0;
	double worst = 0, t0 = us();
	for (int i = 0; i < n; i++) {
		int k = snprintf(msg, sizeof msg, "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\",\"params\":{\"uri\":\"file:///x/A.lean\","
		                 "\"version\":1,\"isIncremental\":%s,\"diagnostics\":[{\"range\":{\"start\":{\"line\":%d,\"character\":0},\"end\":{\"line\":%d,"
		                 "\"character\":7}},\"severity\":1,\"message\":\"%s\"}]}}", i ? "true" : "false", i, i, text);
		double a = us();
		tap(0, msg, (size_t)k);
		double d = us() - a;
		if (d > worst) worst = d;
	}
	printf("%d incremental publications, %d page(s): %.1f ms in all, the slowest %.0f us (the list is then %zu kB)\n",
	       n, np, (us() - t0) / 1000, worst, kept->d.n / 1024);
	return 0;
}
