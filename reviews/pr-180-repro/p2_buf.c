/* the float open at 0,0; another buffer in the same window, cursor 0,0 there */
#include "probe.h"
int main(void)
{
	start(12, 50);
	core_goto(&e, 0, 0);
	keys_input(&e, 'K', 0);
	spin(100);
	show("K at 0,0");
	CHECK(!core_edit_file(&e, "o.c", 0));
	show("after :e o.c");
	return 0;
}
