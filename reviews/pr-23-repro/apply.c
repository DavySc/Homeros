/* Re-review of PR 23 (6fec9e2): oil_apply on a tree that changed after it
   was listed.
     apply swap   a listed directory is replaced by a link to another
                  directory, then deleted in the buffer: is the other
                  directory's file still there?
     apply fifo   a named pipe is copied in the buffer: does oil_apply return? */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "oil.h"
static void apply(const char *dir, const char *text)
{
	const char *dirs[1] = { dir };
	size_t len[1] = { strlen(text) };
	OilAction *act; OilError *err; int nerr, done = 0;
	int na = oil_actions(1, dirs, &text, len, &act, &err, &nerr);
	for (int i = 0; i < na; i++) printf("action %c %c %s %s\n", act[i].type, act[i].etype, act[i].src, act[i].dst ? act[i].dst : "-");
	fflush(stdout);
	const char *e = na > 0 ? oil_apply(act, na, &done) : "no actions";
	printf("oil_apply: %s (%d done)\n", e ? e : "ok", done);
	if (na > 0) oil_free(act, na);
	free(err);
}
int main(int argc, char **argv)
{
	if (argc < 2) return 2;
	if (system("rm -rf /tmp/oa && mkdir -p /tmp/oa/t/d /tmp/oa/keep && echo data > /tmp/oa/keep/precious && echo x > /tmp/oa/t/d/x")) return 2;
	if (!strcmp(argv[1], "swap")) {
		oil_list("/tmp/oa/t/");
		char *r; size_t n;
		oil_render("/tmp/oa/t/", &r, &n);
		printf("listed:\n%.*s", (int)n, r);
		free(r);
		/* outside the editor: d becomes a link to ../keep */
		if (system("rm -rf /tmp/oa/t/d && ln -s ../keep /tmp/oa/t/d")) return 2;
		apply("/tmp/oa/t/", "/000 x  ../\n");            /* the d/ line deleted */
		printf("/tmp/oa/keep/precious %s\n", access("/tmp/oa/keep/precious", F_OK) ? "IS GONE" : "is still there");
	} else {
		if (mkfifo("/tmp/oa/t/pipe", 0600)) return 2;
		oil_list("/tmp/oa/t/");
		char *r; size_t n;
		oil_render("/tmp/oa/t/", &r, &n);
		printf("listed:\n%.*s", (int)n, r);
		/* the pipe's line once more, as "copy" */
		char text[4096], *l = strstr(r, "pipe") - 9;
		while (l > r && l[-1] != '\n') l--;
		snprintf(text, sizeof text, "%.*s%.*scopy\n", (int)n, r, (int)(strstr(l, "pipe") - l), l);
		free(r);
		apply("/tmp/oa/t/", text);
	}
	oil_reset();
	return 0;
}
