/* P5: sessions and buffers: :bd of the query buffer mid-run and after; a too-small screen; two query buffers */
#include "h.h"
static void show(const char *what) { printf("--- %s: buf=%d wins=%d msg=[%s]\n%s\n", what, e.buf->nr, nwins(), e.msg, results()); }
static int conns(void) { return system("docker exec mssql /opt/mssql-tools18/bin/sqlcmd -C -S localhost -U sa -P Homeros_Test1 -h -1 -W -Q \"set nocount on; select 'homeros sessions on the server:', count(*) from sys.dm_exec_sessions where program_name = 'homeros'\""); }
int main(int argc, char **argv)
{
	setup();
	int t = atoi(argv[1]);
	put("ms/a.sql", "select 1 as one\n", 0600);
	put("ms/b.sql", "select 2 as two\n", 0600);
	put("ms/slow.sql", "waitfor delay '00:00:01'\ngo\nselect 'second batch' as x\n", 0600);
	core_open(&e, "ms/a.sql");
	if (t == 1) {   /* many buffers, each run once and wiped */
		conns();
		for (int i = 0; i < 5; i++) {
			core_edit_file(&e, i % 2 ? "ms/a.sql" : "ms/b.sql", 0);
			sql_key(&e, KEY_F5); until("Completion time");
			int nr = e.buf->nr;
			sql_results(&e);
			core_buf_delete(&e, nr, 1);
			printf("buffer %d gone: %d, now in %d [%s]\n", nr, !core_buf_find(&e, nr), e.buf->nr, e.msg);
		}
		for (int i = 0; i < 50; i++) loop_once(10);
		conns();
	}
	if (t == 2) {   /* the query buffer is wiped while its script runs */
		core_edit_file(&e, "ms/slow.sql", 0);
		sql_key(&e, KEY_F5);
		for (int i = 0; i < 30; i++) loop_once(10);
		int nr = e.buf->nr;
		core_buf_delete(&e, nr, 1);
		printf("after :bd: buf=%d msg=[%s] query buffer listed: %d\n", e.buf->nr, e.msg, core_buf_find(&e, nr) != NULL);
		for (int i = 0; i < 200; i++) loop_once(10);
		show("2 s later");
	}
	if (t == 3) {   /* no room for a split */
		for (int i = 0; i < 30; i++) keys("\027s"); printf("splits until: [%s] wins=%d\n", e.msg, nwins());
		printf("F5: %d\n", sql_key(&e, KEY_F5));
		printf("msg=[%s] wins=%d\n", e.msg, nwins());
		core_only(&e);
		printf("F5 with room again: %d msg=[%s] wins=%d\n", sql_key(&e, KEY_F5), e.msg, nwins());
		for (int i = 0; i < 100; i++) loop_once(10);
		show("1 s later");
	}
	if (t == 4) {   /* two query buffers side by side */
		sql_key(&e, KEY_F5); until("Completion time");
		core_split(&e, 1, 0); core_edit_file(&e, "ms/b.sql", 0);
		printf("after :vs wins=%d buf=%d [%s]\n", nwins(), e.buf->nr, e.msg);
		sql_key(&e, KEY_F5);
		for (int i = 0; i < 100; i++) loop_once(10);
		show("F5 in the second query window");
		for (Win *w = e.wins; w; w = w->next) printf("win buf=%d %s y=%d x=%d h=%d w=%d%s\n", w->buf->nr, w->buf->path ? w->buf->path : "(results)", w->y, w->x, w->h, w->w, w == e.win ? " <- cursor" : "");
	}
	core_free(&e);
	return 0;
}
