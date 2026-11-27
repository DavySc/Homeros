/* P9: the server ends the session; P13: the connections file's edges */
#include "h.h"
static void show(const char *what) { printf("--- %s: msg=[%s]\n%s\n", what, e.msg, results()); }
static void killall(void) { system("docker exec mssql /opt/mssql-tools18/bin/sqlcmd -C -S localhost -U sa -P Homeros_Test1 -Q \"declare @s nvarchar(max) = N''; select @s += N'kill ' + cast(session_id as nvarchar) + N';' from sys.dm_exec_sessions where program_name = 'homeros'; exec(@s)\""); }
int main(int argc, char **argv)
{
	setup();
	int t = atoi(argv[1]);
	char conf[4096], f[8192];
	snprintf(conf, sizeof conf, "%s/homeros/sql", dir);
	put("ms/a.sql", "select 1 as one\n", 0600);
	put("ms/slow.sql", "select 'before'\ngo\nwaitfor delay '00:00:03'\ngo\nselect 'after the kill' as x\n", 0600);
	core_open(&e, "ms/a.sql");
	if (t == 1) {
		sql_key(&e, KEY_F5); until("Completion time");
		killall();
		for (int i = 0; i < 50; i++) loop_once(10);
		show("idle, after the server killed the session");
		sql_key(&e, KEY_F5);
		for (int i = 0; i < 100; i++) loop_once(10);
		show("F5 after it");
	}
	if (t == 2) {
		core_edit_file(&e, "ms/slow.sql", 0);
		sql_key(&e, KEY_F5);
		for (int i = 0; i < 100; i++) loop_once(10);
		killall();
		for (int i = 0; i < 400; i++) loop_once(10);
		show("killed while batch 2 of 3 ran");
		printf("F5 again: %d\n", sql_key(&e, KEY_F5));
		for (int i = 0; i < 50; i++) loop_once(10);
		show("the next F5 (first 0.5 s)");
	}
	if (t == 3) {
		const char *sa = getenv("SQL_SA_PASSWORD");
		mkdir("ms/sub", 0700); mkdir("msx", 0700); mkdir("my folder", 0700);
		put("ms/sub/a.sql", "select db_name() as db\n", 0600); put("msx/a.sql", "select 1\n", 0600); put("my folder/a.sql", "select 1\n", 0600);
		snprintf(f, sizeof f, "# comment\nms mssql 127.0.0.1 sa master trust pass=%s\ntmp mssql 127.0.0.1:1433 sa tempdb trust pass=%s\nbadport mssql 127.0.0.1:99999 sa master trust pass=%s\nv6 mssql ::1 sa master trust pass=%s\nnopw mssql 127.0.0.1 sa master trust\ndir %s/ms ms\ndir %s/ms/sub tmp\ndir %s/my folder ms\n", sa, sa, sa, sa, dir, dir, dir);
		put(conf, f, 0600);
		const char *files[] = { "ms/sub/a.sql", "msx/a.sql", "my folder/a.sql" };
		for (int i = 0; i < 3; i++) {
			core_edit_file(&e, files[i], 0); e.msg[0] = 0;
			sql_key(&e, KEY_F5);
			for (int k = 0; k < 100 && !strstr(results(), "Completion"); k++) loop_once(10);
			printf("%s: msg=[%s] results=[%.60s]\n", files[i], e.msg, rwin() ? res : "");
			if (rwin()) sql_results(&e);
		}
		const char *names[] = { "badport", "v6", "nopw", "dir", "" };
		for (int i = 0; i < 5; i++) {
			put("ms/x.sql", "select 1 as one\n", 0600);
			char p[64]; snprintf(p, sizeof p, "ms/x%d.sql", i); put(p, "select 1 as one\n", 0600);
			core_edit_file(&e, p, 0); e.msg[0] = 0;
			int r = sql_connect(&e, names[i]);
			printf(":Connect %s -> %d [%s]\n", names[i], r, e.msg);
			if (!r && *names[i]) { sql_key(&e, KEY_F5); for (int k = 0; k < 300 && !strstr(results(), "Completion") && !strstr(res, "\n"); k++) loop_once(10); printf("   F5: msg=[%s] results=[%.100s]\n", e.msg, rwin() ? res : ""); if (rwin()) sql_results(&e); }
		}
		/* a readable file without a password is fine; a symlink? a long line */
		chmod(conf, 0644);
		core_edit_file(&e, "ms/x0.sql", 0);
		printf(":Connect nopw with 0644: %d [%s]\n", sql_connect(&e, "nopw"), e.msg);
		printf(":Connect ms with 0644: %d [%s]\n", sql_connect(&e, "ms"), e.msg);
	}
	core_free(&e);
	return 0;
}
