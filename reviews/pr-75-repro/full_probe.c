/* file_full on the paths given, from the cwd it is started in:
   gcc -Iinclude -o full_probe full_probe.c src/file/state.c ; ./full_probe path... */
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "file.h"
int main(int argc, char **argv)
{
	char out[PATH_MAX];
	for (int i = 1; i < argc; i++)
		if (file_full(argv[i], out)) printf("-1 %s\n", strerror(errno));
		else puts(out);
}
