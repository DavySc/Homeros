#include <poll.h>
#include <stdio.h>
#include <time.h>
#include "src/sql/pg.h"
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }
int main(void) { Pg g; double worst = 0; if (pg_open(&g, "127.0.0.1", 5432, "slow", "pw", "postgres", 1)) return 1;
	while (g.state != PG_READY && g.state != PG_DEAD) { struct pollfd p = {g.net.fd, POLLIN | (pg_pending(&g) ? POLLOUT : 0), 0}; poll(&p, 1, -1); double t = now(); pg_io(&g, p.revents & POLLIN); if (now() - t > worst) worst = now() - t; }
	printf("state %d, longest single pg_io call %.0f ms (%s)\n", g.state, worst, g.net.err); return 0; }
