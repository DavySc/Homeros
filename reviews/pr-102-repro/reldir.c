/* PR 102 review, finding 1: image_latex with a relative dir.  proc_spawn
   runs the tools in dir, latex gets the file's base name, dvipng gets
   dir/ltximg-... again, which from inside dir names dir/dir/...
   Usage (from the repo root, real latex and dvipng on PATH):
     reldir DIR      DIR relative ("ltx") or absolute ("$PWD/ltx")
   Prints the PNG's path, or "failed" once the render is over without one. */
#include <stdio.h>
#include <string.h>
#include "core.h"
#include "image.h"
#include "loop.h"

int main(int argc, char **argv)
{
	const char *tex = "$\\forall x \\in L,\\ x \\wedge (x \\vee y) = x$", *p;
	if (argc < 2) return 2;
	long long t = loop_now();
	if ((p = image_latex(tex, strlen(tex), 0xcdd6f4, 150, argv[1]))) { printf("already there: %s\n", p); return 0; }
	for (core_changed = 0; !core_changed;) loop_once(100);
	p = image_latex(tex, strlen(tex), 0xcdd6f4, 150, argv[1]);
	printf("%s after %.0f ms\n", p ? p : "failed", (loop_now() - t) / 1e6);
	return !p;
}
