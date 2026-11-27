/* P6: a large result: total time, the longest single turn of the loop, the time Alt+Break takes to show */
#include "h.h"
#include <time.h>
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }
int main(int argc, char **argv)
{
	setup();
	int pg = !strcmp(argv[1], "pg"); long rows = atol(argv[2]); int brk = argc > 3;
	char q[1000];
	snprintf(q, sizeof q, pg ? "select g, g*2 as b, 'some text here' as c, now() as d, g::numeric/7 as e, md5(g::text) as f from generate_series(1, %ld) g\n"
		: "select top %ld row_number() over (order by (select 1)) as g, a.object_id as b, 'some text here' as c, getdate() as d, cast(a.object_id as decimal(18,4))/7 as e, a.name as f from sys.all_objects a, sys.all_objects b, sys.all_objects c\n", rows);
	put(pg ? "pg/a.sql" : "ms/a.sql", q, 0600);
	core_open(&e, pg ? "pg/a.sql" : "ms/a.sql");
	put(pg ? "pg/w.sql" : "ms/w.sql", "select 1\n", 0600);
	/* connect first so the login is not timed */
	core_edit_file(&e, pg ? "pg/a.sql" : "ms/a.sql", 0);
	keys("ggVG"); keys("\033");
	double t0 = now(), worst = 0, tb = 0;
	sql_key(&e, KEY_F5);
	int turns = 0;
	for (;;) {
		double a = now();
		loop_once(10);
		double d = now() - a;
		turns++;
		Win *w = rwin();
		size_t z = w ? text_len(w->buf->text) : 0;
		if (d > worst) worst = d;
		if (brk && !tb && z > 2000000) { tb = now(); sql_key(&e, KEY_BREAK); }
		if (z > 40) { char tail[64]; text_read(w->buf->text, z - 40, tail, 40); tail[40] = 0; if (strstr(tail, "Completion time") || (brk && tb && now() - tb > 3000)) break; if (brk && tb && strstr(tail, "cancelled")) { static int seen; if (!seen++) printf("cancel line shown after %.1f ms\n", now() - tb); } }
		if (now() - t0 > 120000) { printf("gave up\n"); break; }
	}
	Win *w = rwin();
	printf("%s rows=%ld: %.0f ms total, %d loop turns, longest turn %.1f ms, results %zu bytes %zu lines\n", argv[1], rows, now() - t0, turns, worst, text_len(w->buf->text), text_lines(w->buf->text));
	return 0;
}
