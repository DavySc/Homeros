/* P1: the buffer is edited while a script runs */
#include "h.h"
int main(void)
{
	setup();
	put("ms/a.sql", "waitfor delay '00:00:01'\ngo\nselect name from (values ('keep'),('gone')) v(name)\nwhere name = 'keep'\ngo\n", 0600);
	core_open(&e, "ms/a.sql");
	sql_key(&e, KEY_F5);
	for (int i = 0; i < 30; i++) loop_once(10);
	/* Davy types a comment line at the top while batch 1 waits */
	keys("ggO-- a note typed while it runs\033");
	until("Completion time");
	printf("--- results after an edit during the run:\n%s\n", res);
	core_free(&e);
	return 0;
}
