/* the float open, then the screen shrinks */
#include "probe.h"
int main(void)
{
	start(24, 80);
	core_goto(&e, 2, 4);
	keys_input(&e, 'K', 0);
	spin(100);
	show("K at 2,4 on 24x80");
	for (int r = 24; r >= 1; r -= 3) { CHECK(!grid_resize(&g, r, r * 3)); draw(); }
	CHECK(!grid_resize(&g, 4, 12));
	show("4x12");
	return 0;
}
