#include <stdio.h>
#include "core.h"
int main(int argc, char **argv)
{
	static Editor e; (void)argc;
	if (core_open(&e, argv[1])) return 1;
	for (const char *k = argv[2]; *k; k++) keys_feed(&e, *k == '$' ? 27 : *k == '#' ? 13 : *k, k[1] != 0);
	return 0;
}
