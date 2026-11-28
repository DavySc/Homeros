/* PR 179 review: a redraw's time with a SQL buffer shown (the status
   line asks sql_status each frame).  p179t N: microseconds per core_draw. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include "core.h"
#include "grid.h"
#include "keys.h"
static Editor e; static Grid g;
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (double)t.tv_sec * 1e3 + (double)t.tv_nsec / 1e6; }
int main(int argc, char **argv)
{
	char dir[64] = "/tmp/p179tXXXXXX", conf[200];
	mkdtemp(dir), chdir(dir), mkdir("homeros", 0700), mkdir("ms", 0700);
	setenv("XDG_CONFIG_HOME", dir, 1), setenv("HOME", dir, 1);
	snprintf(conf, sizeof conf, "%s/homeros/sql", dir);
	FILE *f = fopen(conf, "w"); fputs("ms mssql 127.0.0.1 sa master trust pass=x\ndir ~/ms ms\n", f), fclose(f), chmod(conf, 0600);
	f = fopen("ms/a.sql", "w"); for (int i = 0; i < 200; i++) fprintf(f, "select a, b, c from t%d where x = %d\n", i, i); fclose(f);
	core_open(&e, "ms/a.sql");
	keys_input(&e, 'j', 0);
	int n = argc > 1 ? atoi(argv[1]) : 20000;
	grid_resize(&g, 50, 200);
	for (int i = 0; i < 500; i++) core_draw(&e, NULL, &g);
	double t = now();
	for (int i = 0; i < n; i++) core_draw(&e, NULL, &g);
	printf("%.3f\n", (now() - t) * 1e3 / n);
	return 0;
}
