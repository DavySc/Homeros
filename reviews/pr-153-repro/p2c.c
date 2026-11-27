/* P2c: Postgres, Alt+Break then (mode) before the loop turns again: 0 nothing, 1 an edit, 2 a refused F5, 3 Break again */
#include "h.h"
int main(int argc, char **argv)
{
	setup();
	int mode = atoi(argv[1]), pg = argc < 3;
	put("pg/b.sql", "select g from generate_series(1, 30000000) g\n", 0600);
	put("ms/b.sql", "select top 3000000 a.object_id from sys.all_objects a, sys.all_objects b, sys.all_objects c\n", 0600);
	core_open(&e, pg ? "pg/b.sql" : "ms/b.sql");
	sql_key(&e, KEY_F5);
	for (int i = 0; i < 40; i++) loop_once(10);
	sql_key(&e, KEY_BREAK);
	if (mode == 1) keys("ggdGiselect 1 as kept\033");
	if (mode == 2) sql_key(&e, KEY_F5);
	if (mode == 3) sql_key(&e, KEY_BREAK);
	int n = 0;
	for (; n < 1000; n++) { loop_once(10); e.msg[0] = 0; if (n % 10 == 9) { if (mode != 1) { keys("ggdGiselect 1 as kept\033"); mode = 1; } sql_key(&e, KEY_F5); if (!strstr(e.msg, "cancel")) break; } }
	printf("F5 accepted after %d loop turns (msg=[%s])\n", n + 1, e.msg);
	until("Completion time");
	printf("%s\n", res);
	return 0;
}
