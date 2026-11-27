/* P1b: an edit inside a later batch while an earlier one runs: the later batch loses its end */
#include "h.h"
int main(void)
{
	setup();
	put("ms/a.sql", "waitfor delay '00:00:01'\ngo\nselect 'would delete' as what, id\nfrom (values (10),(1042)) v(id)\nwhere id = 1042\n", 0600);
	core_open(&e, "ms/a.sql");
	sql_key(&e, KEY_F5);
	for (int i = 0; i < 30; i++) loop_once(10);
	/* two characters typed in the third line while batch 1 waits: "select" becomes "select  " (two spaces) */
	keys("3Gea  \033");
	until("Completion time");
	printf("--- results:\n%s\n", res);
	core_free(&e);
	return 0;
}
