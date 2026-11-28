/* K, then an edit before the answer: what the server is sent (DESIGN.md: "cancel the old request") */
#include "probe.h"
int main(void)
{
	start(12, 50);
	core_goto(&e, 0, 4);
	keys_input(&e, 'K', 0);
	keys_input(&e, 'x', 0);
	spin(100);
	show("K then x");
	keys_input(&e, 'K', 0);
	keys_input(&e, 'K', 0);
	spin(100);
	cat_log();
	return 0;
}
