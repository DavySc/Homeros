#include <stdio.h>
#include <stdlib.h>
#include "lsp.h"
int main(void)
{
	const char *u[] = { "file:///1:/x.h", "file:///a:b/x.h", "file:///%41:/x.h", "file:///tmp/a%2", "file:///tmp/a%zz.h", "file:///tmp/%E2%82%AC" };
	for (int i = 0; i < 6; i++) { char *f = lsp_fname(u[i]); printf("%s -> %s\n", u[i], f); free(f); }
}
