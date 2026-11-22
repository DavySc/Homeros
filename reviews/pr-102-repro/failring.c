/* PR 102 review, finding 3: failed renders are remembered in a ring of 64.
   A screen with more failing fragments than that (latex not installed, as
   on a machine without TeX) never settles: each frame asks for every
   fragment, the ones the ring forgot start again, their failure sets
   core_changed, which asks for the next frame.
   Usage: failring FRAGMENTS FRAMES   (PATH without latex, or with one that fails)
   Prints how many of the last 100 frames asked for another frame. */
#include <stdio.h>
#include <stdlib.h>
#include "core.h"
#include "image.h"
#include "loop.h"

int main(int argc, char **argv)
{
	int k = argc > 1 ? atoi(argv[1]) : 100, frames = argc > 2 ? atoi(argv[2]) : 300, again = 0;
	char tex[32], dir[] = "/tmp/failringXXXXXX";
	if (!mkdtemp(dir)) return 2;
	for (int f = 0; f < frames; f++) {
		core_changed = 0;
		for (int i = 0; i < k; i++) image_latex(tex, (size_t)snprintf(tex, sizeof tex, "$x_{%d}$", i), 0xcdd6f4, 150, dir);
		for (int i = 0; i < 20; i++) loop_once(5);   /* the jobs end */
		if (f >= frames - 100) again += core_changed;
	}
	printf("%d fragments: %d of the last 100 frames asked for another\n", k, again);
	return 0;
}
