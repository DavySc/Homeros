/* P2: F5 straight after Alt+Break; arg: ms|pg, loops to wait between Break and F5 */
#include "h.h"
int main(int argc, char **argv)
{
	setup();
	int pg = !strcmp(argv[1], "pg"), wait = atoi(argv[2]);
	put("ms/b.sql", "create table #t (i int); insert #t values (7);\ngo\nselect top 3000000 a.object_id from sys.all_objects a, sys.all_objects b, sys.all_objects c\n", 0600);
	put("pg/b.sql", "create temp table t as select 7 as i; select g from generate_series(1, 30000000) g\n", 0600);
	core_open(&e, pg ? "pg/b.sql" : "ms/b.sql");
	sql_key(&e, KEY_F5);
	for (int i = 0; i < 40; i++) loop_once(10);
	printf("Break: %d\n", sql_key(&e, KEY_BREAK));
	for (int i = 0; i < wait; i++) loop_once(10);
	/* the buffer becomes a short query on the same session */
	keys(pg ? "ggdGiselect i as kept from t\033" : "ggdGiselect i as kept from #t\033");
	printf("F5: %d msg=[%s]\n", sql_key(&e, KEY_F5), e.msg);
	for (int i = 0; i < 300; i++) loop_once(10); printf("after 3 s: results=[%s]\n", results()); printf("second F5: %d msg=[%s]\n", sql_key(&e, KEY_F5), e.msg); until("Completion time");
	size_t n = strlen(res);
	printf("--- results (last 400 bytes of %zu):\n%s\n", n, n > 400 ? res + n - 400 : res);
	core_free(&e);
	return 0;
}
