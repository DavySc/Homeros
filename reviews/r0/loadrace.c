/* An outside write that lands while file_load is reading must be refused at save. */
#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
#include "file.h"
int main(int argc, char **argv)
{
	const char *p = argv[1];
	(void)argc;
	int fd = open(p, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (ftruncate(fd, 1L << 30)) return 2;       /* 1 GiB, so the read takes a while */
	close(fd);
	pid_t c = fork();
	if (!c) { usleep(atoi(argv[2]) * 1000); fd = open(p, O_WRONLY); if (pwrite(fd, "OUTSIDE", 7, 0) != 7) _exit(1); close(fd); _exit(0); }
	char *d; size_t n; FileId id;
	if (file_load(p, &d, &n, &id)) return 2;
	waitpid(c, NULL, 0);
	int stale = d[0] != 'O';                      /* our buffer does not hold the outside write */
	free(d);
	Text *t = text_new("mine\n", 5);
	char kept[512];
	int r = file_save(p, t, 5, &id, 0, kept, sizeof kept);
	printf("buffer %s the outside write; save returned %s\n", stale ? "lacks" : "has",
	       r == SAVE_OK ? "SAVE_OK (outside write overwritten)" : r == SAVE_CHANGED ? "SAVE_CHANGED" : "other");
	unlink(p);
	return 0;
}
