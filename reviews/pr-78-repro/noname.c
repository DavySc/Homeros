/* PR 78 probe: Insert mode in a buffer with no file name (homeros started with no file) */
#include <stdio.h>
#include "core.h"
#include "snip.h"
int main(void)
{
	static Editor e;
	if (core_open(&e, NULL)) return 2;
	keys_feed(&e, 'i', 0);
	if (!snip_key(&e, 'x', 0)) keys_feed(&e, 'x', 0);
	puts("no UB");
	return 0;
}
