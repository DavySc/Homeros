/* the same answer asked twice in one session: the first float against the second */
#include "probe.h"
int main(void)
{
	start(24, 60);
	core_goto(&e, 2, 4);
	keys_input(&e, 'K', 0);
	spin(100);
	show("first K at 2,4");
	core_goto(&e, 2, 5); draw();
	core_goto(&e, 2, 4);
	keys_input(&e, 'K', 0);
	spin(100);
	show("second K at 2,4");
	return 0;
}
