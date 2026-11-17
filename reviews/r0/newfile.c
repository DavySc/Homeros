#include <stdio.h>
#include <sys/stat.h>
#include "core.h"
int main(int argc, char **argv)
{
	static Editor e; struct stat st; (void)argc;
	if (core_open(&e, argv[1]) || core_edit(&e, 0, 0, "hi", 2) || core_write(&e, 0)) return 1;
	stat(argv[1], &st);
	printf("mode %o\n", st.st_mode & 0777);
	return 0;
}
