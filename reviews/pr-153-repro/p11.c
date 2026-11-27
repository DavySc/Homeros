/* P11: the rules my plants found untested: F5 during a run; a three-batch run after a cancel; a selection in the results; a port with letters */
#include "h.h"
int main(void)
{
	setup();
	put("ms/slow.sql", "waitfor delay '00:00:01'\ngo\nselect 'second' as x\n", 0600);
	put("ms/big.sql", "select top 3000000 a.object_id from sys.all_objects a, sys.all_objects b, sys.all_objects c\n", 0600);
	put("ms/three.sql", "select 'b1' as x\ngo\nselect 'b2' as x\ngo\nselect 'b3' as x\n", 0600);
	core_open(&e, "ms/slow.sql");
	sql_key(&e, KEY_F5);
	for (int i = 0; i < 30; i++) loop_once(10);
	e.msg[0] = 0; sql_key(&e, KEY_F5);
	printf("F5 during a run: [%s]\n", e.msg);
	until("Completion time"); printf("%s\n", res);
	core_edit_file(&e, "ms/big.sql", 0);
	sql_key(&e, KEY_F5);
	for (int i = 0; i < 40; i++) loop_once(10);
	sql_key(&e, KEY_BREAK);
	for (int i = 0; i < 200; i++) loop_once(10);
	keys("ggdGiselect 'b1' as x\rgo\rselect 'b2' as x\rgo\rselect 'b3' as x\033");
	e.msg[0] = 0; sql_key(&e, KEY_F5); printf("F5 after the cancel: [%s]\n", e.msg);
	until("Completion time"); printf("%s\n", res);
	/* a selection made in the results window */
	core_win_goto(&e, 'j', 1);
	keys("ggjv");
	sql_key(&e, KEY_F5);
	for (int i = 0; i < 50; i++) loop_once(10);
	printf("F5 with a v selection in the results:\n%s\n", results());
	return 0;
}
