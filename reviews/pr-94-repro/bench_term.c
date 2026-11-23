/* term_draw cost when a program paints many 24-bit colours: one draw =
   core_draw + tools_draw on an unchanged screen; median of 201 draws. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "core.h"
#include "keys.h"
#include "loop.h"
#include "tools.h"
static Editor e;
static Grid g;
static long long ns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1000000000LL + t.tv_nsec; }
static void settle(void) { long long t0 = ns(); while (ns() - t0 < 500000000LL) { core_changed = 0; loop_once(300); if (core_changed) t0 = ns(); } }
static int cmp(const void *a, const void *b) { long long x = *(long long *)a, y = *(long long *)b; return x < y ? -1 : x > y; }
int main(int argc, char **argv)
{
	int ncol = atoi(argv[1]);   /* distinct colours painted */
	setenv("SHELL", "/bin/sh", 1); setenv("PS1", "", 1); setenv("ENV", "/dev/null", 1);
	core_open(&e, NULL); grid_resize(&g, 50, 200);
	core_draw(&e, &g); tools_draw(&e, &g);
	tools_key(&e, KEY_CTRL_DOT); settle(); core_draw(&e, &g); tools_draw(&e, &g);
	char cmd[300];
	snprintf(cmd, sizeof cmd, "clear; i=0; while [ $i -lt 7000 ]; do c=$((i %% %d)); printf '\\033[38;2;%%d;%%d;7m#' $((c/256)) $((c%%256)); i=$((i+1)); done\n", ncol);
	for (char *s = cmd; *s; s++) tools_key(&e, *s == '\n' ? KEY_ENTER : (unsigned char)*s);
	settle();
	long long t[201];
	for (int i = 0; i < 201; i++) { long long a = ns(); core_draw(&e, &g); tools_draw(&e, &g); t[i] = ns() - a; }
	qsort(t, 201, sizeof *t, cmp);
	printf("colours %d: median %.3f ms (min %.3f max %.3f)\n", ncol, t[100] / 1e6, t[0] / 1e6, t[200] / 1e6);
	return 0;
}
