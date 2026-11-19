/* Recent files against Neovim: the sessions src/shada/conform.py ran in
   Neovim 0.12 run here through the core, on the same files and the same
   clock, and the file shada_write leaves must list Neovim's v:oldfiles; each
   session must also read the last one's as its own v:oldfiles. */
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "check.h"
#include "core.h"
#include "shada.h"
#include "../src/shada/shada-expect.inc"

/* Neovim's clock was tools/fake_time.c's; ours is this one */
static time_t now;

int shada_removable(const char *path);   /* shada.c's, not in shada.h */
time_t time(time_t *t) { if (t) *t = now; return now; }

static char state[4096];

/* the file's paths, in order, as indexes into files[]; -1 for a path not there */
static int written(int *idx, int cap)
{
	FILE *f = fopen(state, "r");
	if (!f) return 0;
	char line[4096];
	int n = 0;
	while (fgets(line, sizeof line, f) && n < cap) {
		char *p = strchr(line, ' ');
		CHECK(p);
		p[1 + strcspn(p + 1, "\n")] = 0;
		idx[n] = -1;
		for (int i = 0; i < (int)(sizeof files / sizeof *files); i++) if (!strcmp(p + 1, files[i])) idx[n] = i;
		n++;
	}
	fclose(f);
	return n;
}

static void run(int k, int s)
{
	static Editor e;
	now = sessions[k][s].t0;
	CHECK(!shada_read());
	/* v:oldfiles is what the session before left */
	int prev = s ? sessions[k][s - 1].nold : 0;
	for (int i = 0; i < prev; i++) CHECK(shada_oldfile((size_t)i) && !strcmp(shada_oldfile((size_t)i), files[sessions[k][s - 1].old[i]]));
	CHECK(!shada_oldfile((size_t)prev));
	int start = sessions[k][s].start;
	CHECK(!core_open(&e, start >= 0 ? files[start] : NULL));
	shada_track(&e);
	for (const char *a = sessions[k][s].acts; *a; a++) {
		now += 2;
		if (*a == 'e') CHECK(!core_edit_file(&e, files[*++a - '0'], 0));
		else core_buf_delete(&e, e.buf->nr, 0);   /* :bd; E516-like refusals are Neovim's too */
		shada_track(&e);
	}
	now += 2;
	CHECK(!shada_write(&e));
	core_free(&e);
}

/* Measured by hand in Neovim 0.12 (one run each, the commands in the
   comments): one session leaving 150 files keeps the 100 newest (f150 to
   f51), and a later session that left f150 and f149 at older times keeps
   the file's newer ones (f150, f149, f148, f147 first). */
static void many(int older)
{
	static Editor e;
	char f[64];
	CHECK(!core_open(&e, NULL));
	shada_track(&e);
	for (int i = older ? 150 : 1; older ? i >= 149 : i <= 150; i += older ? -1 : 1) {
		now = older ? 900 + 2 * (150 - i) : 1000 + 2 * i;
		snprintf(f, sizeof f, "/var/tmp/homeros-shada/many/f%d", i);
		CHECK(!core_edit_file(&e, f, 0));
		shada_track(&e);
	}
	now = older ? 904 : 2000;
	CHECK(!shada_write(&e));
	core_free(&e);
}

static void many_check(int older)
{
	pid_t p = fork();
	CHECK(p >= 0);
	if (!p) { many(older); exit(0); }
	int st;
	CHECK(waitpid(p, &st, 0) == p && WIFEXITED(st) && !WEXITSTATUS(st));
	FILE *fp = fopen(state, "r");
	CHECK(fp);
	char line[256], first[4][64] = { "" }, last[64] = "";
	int n = 0;
	while (fgets(line, sizeof line, fp)) {
		char *q = strrchr(line, '/');
		CHECK(q);
		q[1 + strcspn(q + 1, "\n")] = 0;
		if (n < 4) strcpy(first[n], q + 1);
		strcpy(last, q + 1);
		n++;
	}
	fclose(fp);
	CHECK(n == 100 && !strcmp(first[0], "f150") && !strcmp(first[1], "f149"));
	if (!older) CHECK(!strcmp(last, "f51"));
	else CHECK(!strcmp(first[2], "f148") && !strcmp(first[3], "f147"));
}

int main(void)
{
	const char *x = getenv("XDG_STATE_HOME");
	CHECK(x && *x);
	snprintf(state, sizeof state, "%s/homeros/oldfiles", x);
	mkdir("/var/tmp/homeros-shada", 0755);
	mkdir("/var/tmp/homeros-shada/sub", 0755);
	for (size_t i = 0; i < sizeof files / sizeof *files; i++) {
		if (strstr(files[i], "/new")) { unlink(files[i]); continue; }
		FILE *f = fopen(files[i], "w");
		CHECK(f && fputs("x\ny\n", f) >= 0 && !fclose(f));
	}
	int nk = (int)(sizeof sessions / sizeof *sessions);
	for (int k = 0; k < nk; k++) {
		unlink(state);
		for (int s = 0; s < 3; s++) {
			/* a process per session: shada.c's state is per process, as Neovim's */
			pid_t p = fork();
			CHECK(p >= 0);
			if (!p) { run(k, s); exit(0); }
			int st;
			CHECK(waitpid(p, &st, 0) == p && WIFEXITED(st) && !WEXITSTATUS(st));
			int idx[128], n = written(idx, 128);
			int ok = n == sessions[k][s].nold;
			for (int i = 0; ok && i < n; i++) ok = idx[i] == sessions[k][s].old[i];
			if (!ok) {
				fprintf(stderr, "scenario %d session %d (start %d, %s):\n got ", k, s, sessions[k][s].start, sessions[k][s].acts);
				for (int i = 0; i < n; i++) fprintf(stderr, " %d", idx[i]);
				fprintf(stderr, "\n want");
				for (int i = 0; i < sessions[k][s].nold; i++) fprintf(stderr, " %d", sessions[k][s].old[i]);
				fprintf(stderr, "\n");
				CHECK(0);
			}
		}
	}
	/* 'shada' '100, and a merge keeps the newer time */
	unlink(state);
	mkdir("/var/tmp/homeros-shada/many", 0755);
	for (int i = 1; i <= 150; i++) {
		char f[64];
		snprintf(f, sizeof f, "/var/tmp/homeros-shada/many/f%d", i);
		FILE *fp = fopen(f, "w");
		CHECK(fp && !fclose(fp));
	}
	many_check(0);
	many_check(1);
	/* file_full, Neovim's fix_fname: the directory resolved, relative or
	   through a link, the last part kept as typed, a link too */
	char d[PATH_MAX], want[PATH_MAX + 16], got[PATH_MAX];
	CHECK(realpath("/var/tmp/homeros-shada", d) && !chdir(d));
	unlink("lnd"); unlink("lnf");
	CHECK(!symlink("sub", "lnd") && !symlink("a", "lnf"));
	snprintf(want, sizeof want, "%s/sub/d", d);
	CHECK(!file_full("lnd/../sub/d", got) && !strcmp(got, want));
	CHECK(!file_full("lnd/d", got) && !strcmp(got, want));
	snprintf(want, sizeof want, "%s/lnf", d);
	CHECK(!file_full("lnf", got) && !strcmp(got, want));
	/* the default 'shada' r/tmp/,r/private/, ignoring case: Neovim 0.12
	   lists neither /private/ nor /TMP/ (measured in PR 73's review, 3
	   runs); /tmp/ through the core is in the sessions above */
	CHECK(shada_removable("/private/x.sql") && shada_removable("/TMP/y.sql") && shada_removable("/tmp/z.sql"));
	CHECK(!shada_removable("/var/tmp/a") && !shada_removable("/privatex") && !shada_removable("/tm/"));
	return 0;
}
