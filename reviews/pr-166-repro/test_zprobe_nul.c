/* Review of PR 166, not this PR's: SPC, a group's key, then the key 0
   (Ctrl-@, Ctrl-Space in a terminal) reads past the map's string in map():
   typed() matches the 0 against the string's end.  Same on main 9802abcb. */
#include <stdio.h>
#include "core.h"
#include "keys.h"

int main(void)
{
	static Editor e;
	if (core_open(&e, NULL)) return 99;
	keys_input(&e, ' ', 1);
	keys_input(&e, '5', 1);
	keys_input(&e, 0, 0);
	printf("no overflow seen\n");
	core_free(&e);
	return 0;
}
