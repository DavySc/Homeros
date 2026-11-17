/* Loads argv[1], then saves "editor text\n" over it with file_save while
   shim.so lets an outside writer in before one chosen call.  Prints what
   file_save said and where the outside text is afterwards.  argv[2] = "link"
   gives the file a second hard link first (the in-place path); "ro" makes
   the directory read-only (in place too, when not root).
   build: gcc -Iinclude -o steps steps.c src/file/*.c src/text/text.c */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "file.h"
static int has(const char *p, const char *what)
{
	char b[4096] = "";
	FILE *f = fopen(p, "r");
	if (!f) return 0;
	size_t n = fread(b, 1, sizeof b - 1, f);
	b[n] = 0;
	fclose(f);
	return strstr(b, what) != NULL;
}
int main(int argc, char **argv)
{
	static const char *names[] = { "OK", "CHANGED", "KEPT", "ERR" };
	char *data, kept[4096] = "", l[4096];
	size_t n;
	FileId id;
	const char *p = argv[1];
	FILE *f = fopen(p, "w"); fputs("original\n", f); fclose(f);
	snprintf(l, sizeof l, "%s.link", p);
	unlink(l);
	if (argc > 2 && !strcmp(argv[2], "link") && link(p, l)) return 2;
	if (file_load(p, &data, &n, &id)) return 2;
	Text *t = text_new("editor text\n", 12);
	setenv("SHIM_ARMED", "1", 1);
	int r = file_save(p, t, 12, &id, 0, kept, sizeof kept);
	unsetenv("SHIM_ARMED");
	int infile = has(p, "OUTSIDE"), inkept = *kept && has(kept, "OUTSIDE");
	int ours = has(p, "editor text"), wrote = has(p, "OUTSIDE") || access(l, F_OK) == 0 || 1;
	(void)wrote;
	if (!infile && !inkept && !getenv("SHIM_RAN")) { puts("save finished before this step"); return 0; }
	printf("%-7s outside text %s; editor text %s\n", names[r],
	       infile ? "in the file" : inkept ? "kept aside" : "GONE  <-- lost",
	       ours ? "in the file" : "not in the file");
	if (*kept) unlink(kept);
	return 0;
}
