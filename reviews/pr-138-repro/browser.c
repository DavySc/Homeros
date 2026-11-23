/* PR 138's review: the editor opens the browser (proc_open) and exits.
   cc -Iinclude -o build/browser reviews/pr-138-repro/browser.c src/loop/loop.c src/loop/timer.c */
#include "loop.h"
int main(int argc, char **argv)
{
	if (argc < 2 || proc_open(argv[1])) return 1;
	for (int i = 0; i < 50; i++) loop_once(10);
	return 0;
}
