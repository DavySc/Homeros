#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "file.h"
int main(int argc, char **argv)
{
	char *d, kept[512], b[64] = ""; size_t n; FileId id; (void)argc;
	FILE *f = fopen(argv[1], "w"); fputs("loaded\n", f); fclose(f);
	if (file_load(argv[1], &d, &n, &id)) return 2;
	Text *t = text_new("first save\n", 11);
	setenv("OUTSIDE", "1", 1);                    /* the outside write lands right after this save's swap */
	printf("save 1: %d\n", file_save(argv[1], t, 11, &id, 0, kept, sizeof kept));
	text_insert(t, 0, "2", 1);
	int r = file_save(argv[1], t, 12, &id, 0, kept, sizeof kept);
	f = fopen(argv[1], "r"); if (!fgets(b, sizeof b, f)) b[0] = 0; fclose(f);
	printf("save 2: %s; file now: %s", r == SAVE_CHANGED ? "SAVE_CHANGED (refused)" : r == SAVE_OK ? "SAVE_OK" : "other", b);
	return 0;
}
