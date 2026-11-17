/* proc_write of 0 bytes to a child with an empty queue: memcpy(NULL, b, 0). */
#include <stdio.h>
#include "loop.h"
static void fn(void *c, int s, const char *b, size_t n) { (void)c; (void)s; (void)b; (void)n; }
int main(void)
{
	char *argv[] = { "cat", NULL };
	int id = proc_spawn(argv, NULL, fn, NULL);
	printf("write of 0 bytes: %d\n", proc_write(id, "", 0));
	return 0;
}
