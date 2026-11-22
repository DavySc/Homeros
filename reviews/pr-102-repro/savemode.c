/* PR 102 review, finding 1: file_save no longer fills the struct stat it
   then reads (st_nlink, st_mode, st_uid, st_gid).  Saves a 0600 file through
   the core, as :w does, and prints the mode it has afterwards.
   Usage: savemode FILE */
#include <stdio.h>
#include <sys/stat.h>
#include "core.h"

int main(int argc, char **argv)
{
	static Editor e;
	struct stat st;
	FILE *f = fopen(argv[argc - 1], "w");
	if (argc < 2 || !f) return 2;
	fputs("one\n", f);
	fclose(f);
	chmod(argv[1], 0600);
	if (core_open(&e, argv[1]) || core_edit(&e, 0, 0, "m", 1)) return 2;
	int r = core_write(&e, e.buf, 0);
	stat(argv[1], &st);
	printf("core_write %d, mode before 0600, after %04o\n", r, (unsigned)(st.st_mode & 07777));
	core_free(&e);
	return 0;
}
