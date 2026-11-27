/* P10 (second pass): new paths: Break at login, F5 from the results, :Disconnect mid-run, many sessions, Break twice, a kill while cancelling */
#include "h.h"
static void show(const char *what) { printf("--- %s: buf=%d wins=%d msg=[%s]\n%.300s\n", what, e.buf->nr, nwins(), e.msg, results()); }
static void killall(void) { system("docker exec mssql /opt/mssql-tools18/bin/sqlcmd -C -S localhost -U sa -P Homeros_Test1 -Q \"declare @s nvarchar(max) = N''; select @s += N'kill ' + cast(session_id as nvarchar) + N';' from sys.dm_exec_sessions where program_name = 'homeros'; exec(@s)\" > /dev/null"); }
int main(int argc, char **argv)
{
	setup();
	int t = atoi(argv[1]);
	put("ms/a.sql", "select 1 as one\n", 0600);
	put("ms/big.sql", "create table #t (i int)\ngo\nselect top 3000000 a.object_id from sys.all_objects a, sys.all_objects b, sys.all_objects c\ngo\nselect 'must not run' as x\n", 0600);
	core_open(&e, "ms/a.sql");
	if (t == 1) {   /* Break before the login is done, then F5 */
		sql_key(&e, KEY_F5);
		printf("Break at once: %d\n", sql_key(&e, KEY_BREAK));
		for (int i = 0; i < 30; i++) loop_once(10);
		show("after Break at login");
		sql_key(&e, KEY_F5); until("Completion time"); show("F5 after it");
	}
	if (t == 2) {   /* F5 from the results window, also with a selection there */
		sql_key(&e, KEY_F5); until("Completion time");
		core_win_goto(&e, 'j', 1);
		printf("in results: buf nonumber=%d\n", e.buf->nonumber);
		keys("ggVG");
		e.msg[0] = 0;
		printf("F5 in results with V: %d\n", sql_key(&e, KEY_F5));
		for (int i = 0; i < 50; i++) loop_once(10);
		show("after");
		keys("\033");
		keys("x"); printf("x in results: [%s]\n", e.msg);
	}
	if (t == 3) {   /* :Disconnect mid-run, then F5; Break twice; a kill while cancelling */
		core_edit_file(&e, "ms/big.sql", 0);
		sql_key(&e, KEY_F5);
		for (int i = 0; i < 40; i++) loop_once(10);
		keys(":Disconnect\r"); printf(":Disconnect mid-run: [%s]\n", e.msg);
		for (int i = 0; i < 30; i++) loop_once(10);
		size_t n = strlen(results()); printf("results end: [%s]\n", n > 80 ? res + n - 80 : res);
		sql_key(&e, KEY_F5);
		for (int i = 0; i < 40; i++) loop_once(10);
		printf("Break: %d, again: %d\n", sql_key(&e, KEY_BREAK), sql_key(&e, KEY_BREAK));
		killall();
		for (int i = 0; i < 100; i++) loop_once(10);
		n = strlen(results()); printf("after a kill while cancelling: [%s]\n", n > 160 ? res + n - 160 : res);
		e.msg[0] = 0;
		core_edit_file(&e, "ms/a.sql", 0);
		core_edit_file(&e, "ms/big.sql", 0);
		keys("ggdGiselect 5 as five\033");
		printf("F5: %d [%s]\n", sql_key(&e, KEY_F5), e.msg); until("Completion time"); show("F5 after it");
	}
	if (t == 4) {   /* 20 query buffers, each connected */
		for (int i = 0; i < 20; i++) {
			char p[64]; snprintf(p, sizeof p, "ms/q%d.sql", i); put(p, "select @@spid as spid\n", 0600);
			core_edit_file(&e, p, 0); e.msg[0] = 0;
			sql_key(&e, KEY_F5);
			for (int k = 0; k < 200 && !strstr(results(), "Completion") && !strstr(res, "too many"); k++) loop_once(10);
			printf("%d: msg=[%s] %s\n", i, e.msg, strstr(res, "Completion") ? "ran" : res);
		}
		keys(":Disconnect\r"); printf("[%s]\n", e.msg);
	}
	return 0;
}
