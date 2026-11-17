/* Reference loop: poll(2) over a fixed table. */
#include <errno.h>
#include <poll.h>
#include "loop.h"

enum { MAXW = 16 };
static struct pollfd pf[MAXW];
static struct { LoopFn fn; void *ctx; } w[MAXW];
static int nw;
int loop_int;

int loop_watch(int fd, LoopFn fn, void *ctx)
{
	if (nw == MAXW) return -1;
	pf[nw] = (struct pollfd){ .fd = fd, .events = POLLIN };
	w[nw].fn = fn; w[nw].ctx = ctx;
	nw++;
	return 0;
}

void loop_unwatch(int fd)
{
	for (int i = 0; i < nw; i++)
		if (pf[i].fd == fd) { pf[i] = pf[--nw]; w[i] = w[nw]; return; }
}

int loop_waiting(void) { return poll(pf, (nfds_t)nw, 0) > 0; }

int loop_once(int timeout_ms)
{
	int r = poll(pf, (nfds_t)nw, timeout_ms);
	if (r < 0) return errno == EINTR ? 0 : -1;
	for (int i = 0; i < nw && r > 0; i++)
		if (pf[i].revents) { r--; w[i].fn(w[i].ctx, pf[i].fd); }
	return 0;
}
