/* K at line 0, cursor to line 1, K again before the first answer: the
   first answer (for line 0) must not show at line 1, whose own answer is null */
#include "probe.h"
int main(void)
{
	start(12, 50);
	core_goto(&e, 0, 4);
	keys_input(&e, 'K', 0);
	core_goto(&e, 1, 4);
	keys_input(&e, 'K', 0);
	spin(100);
	show("K at 0,4; cursor to 1,4; K (answers: line 0 text, line 1 null)");
	cat_log();
	return 0;
}
