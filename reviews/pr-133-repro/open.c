#include <stdio.h>
#include <time.h>
#include "src/sql/net.h"
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }
int main(int argc, char **argv) { (void)argc; Net c; for (int trust = 1; trust >= 0; trust--) for (int i = 0; i < 3; i++) { double t = now(); int r = net_open(&c, argv[1], 1433, trust); printf("net_open(%s, trust %d): %d in %.1f ms %s\n", argv[1], trust, r, now() - t, c.err); net_close(&c); } return 0; }
