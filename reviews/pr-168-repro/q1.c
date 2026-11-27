/* Q1: F5 on a connection whose host is unknown: what the results say */
#include "h.h"
int main(void)
{
	setup();
	char conf[4096], f[4096];
	snprintf(conf, sizeof conf, "%s/homeros/sql", dir);
	snprintf(f, sizeof f, "bad mssql nohost.invalid sa master trust pass=x\nbadpg pg nohost.invalid postgres postgres trust pass=x\ndir %s/ms bad\ndir %s/pg badpg\n", dir, dir);
	put(conf, f, 0600);
	put("ms/a.sql", "select 1 as one\n", 0600);
	put("pg/a.sql", "select 1 as one\n", 0600);
	core_open(&e, "ms/a.sql");
	int r = sql_key(&e, KEY_F5);
	for (int i = 0; i < 20; i++) loop_once(10);
	printf("mssql F5=%d msg=[%s] results=[%s]\n", r, e.msg, results());
	core_edit_file(&e, "pg/a.sql", 0);
	r = sql_key(&e, KEY_F5);
	for (int i = 0; i < 20; i++) loop_once(10);
	printf("pg F5=%d msg=[%s] results=[%s]\n", r, e.msg, results());
	return 0;
}
