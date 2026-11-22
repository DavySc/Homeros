/* PR 102 review, note: a fragment TeX never finishes (\loop\iftrue\repeat).
   Asks for it, runs the loop 3 s, then exits as the editor would.
   Usage: spin DIR [MS]; then look for the latex process. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core.h"
#include "image.h"
#include "loop.h"

int main(int argc, char **argv)
{
	const char *tex = "\\loop\\iftrue\\repeat";
	if (argc < 2) return 2;
	image_latex(tex, strlen(tex), 0, 150, argv[1]);
	for (long long end = loop_now() + (argc > 2 ? atoll(argv[2]) : 3000) * 1000000LL; loop_now() < end && !core_changed;) loop_once(100);
	printf("at the end: %s\n", core_changed ? "the job ended" : "the job still runs");
	return 0;
}
