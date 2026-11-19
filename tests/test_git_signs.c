/* Package 17's file-buffer side, on ref/core.c's one buffer: gutter signs
   and SPC h against gitsigns' rule computed here from git diff -U0 (an
   oracle that shares no code with git_sign: it reads git's zero-context
   hunks, the package reads the 3-line-context diff of its status refresh);
   then SPC g's views and SPC g B's address.  Random edits, seeded. */
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "check.h"
#include "core.h"
#include "git.h"
#include "loop.h"
#include "../src/git/gitint.h"

static char tmp[PATH_MAX], repo[PATH_MAX + 8], file[PATH_MAX + 16];
static Editor e;
static int fails;

static char *run(const char *fmt, ...)
{
	char cmd[8192];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(cmd, sizeof cmd, fmt, ap);
	va_end(ap);
	FILE *p = popen(cmd, "r");
	CHECK(p);
	size_t n = 0, cap = 4096, k;
	char *s = malloc(cap);
	CHECK(s);
	while ((k = fread(s + n, 1, cap - n - 1, p)) > 0)
		if ((n += k) + 1 == cap) { cap *= 2; s = realloc(s, cap); CHECK(s); }
	s[n] = 0;
	pclose(p);
	return s;
}

static void settle(void)
{
	for (int i = 0; i < 400; i++) {
		CHECK(loop_once(i < 2 ? 5 : 20) == 0);
		Repo *r = git_repos;
		if (i >= 2 && r && !r->busy && !r->dirty && !r->qn) return;
	}

	CHECK(!"the repository never settled");
}

/* lualine's branch, once git has read the change */
static void branch_check(const char *want)
{
	const char *got = NULL;
	for (int i = 0; i < 200 && (!(got = git_branch(e.buf)) || strcmp(got, want)); i++) CHECK(loop_once(20) == 0);
	if (!got || strcmp(got, want)) { fprintf(stderr, "git_branch %s, lualine's %s\n", got ? got : "NULL", want); fails++; }
}

/* the file's lines, numbered from 1 */
enum { MAXL = 200 };
static char *line[MAXL + 1];
static int nline;

static void write_file(void)
{
	FILE *f = fopen(file, "w");
	CHECK(f);
	for (int i = 1; i <= nline; i++) fprintf(f, "%s\n", line[i]);
	fclose(f);
}

/* gitsigns: a hunk of b removed, d added lines at c marks c .. c+d-1, the
   first min(b, d) changed, the rest added; d = 0 marks line c ('_'), line
   1 when c is 0.  owner[l]: the hunk marking l, -1 none, -2 two hunks */
typedef struct { long a, b, c, d; } Hunk;
static Hunk hk[MAXL];
static int nh, sign[MAXL + 2], owner[MAXL + 2];

static void oracle(void)
{
	char *o = run("cd '%s' && git diff -U0 -- f.txt", repo);
	nh = 0;
	for (int l = 0; l <= MAXL + 1; l++) { sign[l] = 0; owner[l] = -1; }
	for (char *p = o; (p = strstr(p, "\n@@ -")); p++) {
		Hunk h = { 0, 1, 0, 1 };
		char *q;
		h.a = strtol(p + 5, &q, 10);
		if (*q == ',') h.b = strtol(q + 1, &q, 10);
		h.c = strtol(strchr(q, '+') + 1, &q, 10);
		if (*q == ',') h.d = strtol(q + 1, &q, 10);
		hk[nh] = h;
		for (long i = 0; i < (h.d ? h.d : 1); i++) {
			long l = h.d ? h.c + i : h.c ? h.c : 1;
			CHECK(l <= MAXL + 1);
			if (owner[l] == -1) { sign[l] = !h.d ? '_' : i < h.b ? '~' : '+'; owner[l] = nh; }
			else owner[l] = -2;
		}
		nh++;
	}
	free(o);
}

static void compare_signs(const char *when)
{
	oracle();
	for (int l = 1; l <= nline; l++) {
		int got = git_sign(e.buf, (size_t)l - 1), want = owner[l] == -2 ? got : sign[l];
		if (got != want) { fprintf(stderr, "%s: line %d sign '%c', gitsigns '%c'\n", when, l, got ? got : '.', want ? want : '.'); fails++; }
	}
}

/* the text after hunk h is taken back (side 0: the index's lines replace
   the work tree's) or applied to the index (side 1) */
static char *expect(const Hunk *h, int side)
{
	char *o = run("cd '%s' && git diff -U0 -- f.txt", repo);
	char hd[64];
	snprintf(hd, sizeof hd, "\n@@ -%ld", h->a);
	char *p = strstr(o, hd);
	CHECK(p);
	p = strchr(p + 1, '\n') + 1;
	char *idx = run("cd '%s' && git show :f.txt", repo);
	/* the old side's lines (index) and the new side's (work tree) */
	char *base = side ? idx : NULL;
	size_t cap = strlen(o) + strlen(idx) + 4096, n = 0;
	char *out = malloc(cap);
	CHECK(out);
	if (side) {
		/* index lines 1 .. a-1 (or a when b = 0), the + lines, the rest after a+b-1 */
		long keep = h->b ? h->a - 1 : h->a, l = 1;
		char *s = base;
		for (; l <= keep && *s; l++) { char *e2 = strchr(s, '\n'); memcpy(out + n, s, (size_t)(e2 + 1 - s)); n += (size_t)(e2 + 1 - s); s = e2 + 1; }
		for (char *t = p; *t == '-' || *t == '+' || *t == '\\'; t = strchr(t, '\n') + 1)
			if (*t == '+') { char *e2 = strchr(t, '\n'); memcpy(out + n, t + 1, (size_t)(e2 - t)); n += (size_t)(e2 - t); }
		for (long k = 0; k < h->b && *s; k++) s = strchr(s, '\n') + 1;
		strcpy(out + n, s);
	} else {
		long keep = h->d ? h->c - 1 : h->c;
		for (long l = 1; l <= keep; l++) n += (size_t)sprintf(out + n, "%s\n", line[l]);
		for (char *t = p; *t == '-' || *t == '+' || *t == '\\'; t = strchr(t, '\n') + 1)
			if (*t == '-') { char *e2 = strchr(t, '\n'); memcpy(out + n, t + 1, (size_t)(e2 - t)); n += (size_t)(e2 - t); }
		for (long l = keep + h->d + 1; l <= nline; l++) n += (size_t)sprintf(out + n, "%s\n", line[l]);
		out[n] = 0;
	}
	free(o);
	free(idx);
	return out;
}

/* one random change to the work tree's lines */
static void edit(int round)
{
	int k = (int)rnd(3), at = 1 + (int)rnd((unsigned)nline);
	char buf[64];
	if (k == 0 || nline < 4) {                       /* change 1 to 3 lines */
		for (int i = 0; i < 1 + (int)rnd(3) && at + i <= nline; i++) {
			snprintf(buf, sizeof buf, "changed %d.%d", round, i);
			free(line[at + i]); line[at + i] = strdup(buf);
		}
	} else if (k == 1 && nline < MAXL - 4) {          /* insert 1 to 3 lines before at (or at the end) */
		int m = 1 + (int)rnd(3);
		if (rnd(5) == 0) at = nline + 1;
		memmove(line + at + m, line + at, (size_t)(nline + 1 - at) * sizeof *line);
		for (int i = 0; i < m; i++) { snprintf(buf, sizeof buf, "added %d.%d", round, i); line[at + i] = strdup(buf); }
		nline += m;
	} else {                                          /* delete 1 to 2 lines */
		int m = 1 + (int)rnd(2);
		if (at + m - 1 > nline) m = nline - at + 1;
		for (int i = 0; i < m; i++) free(line[at + i]);
		memmove(line + at, line + at + m, (size_t)(nline + 1 - at - m) * sizeof *line);
		nline -= m;
	}
}

static void reload(void)
{
	/* the work tree as the file now is (after SPC h r) */
	FILE *f = fopen(file, "r");
	CHECK(f);
	for (int i = 1; i <= nline; i++) free(line[i]);
	nline = 0;
	char b[256];
	while (fgets(b, sizeof b, f)) { b[strcspn(b, "\n")] = 0; CHECK(nline < MAXL); line[++nline] = strdup(b); }
	fclose(f);
}

static void spc_h(int round)
{
	oracle();
	int cand[MAXL], nc = 0;
	for (int l = 1; l <= nline; l++) if (owner[l] >= 0) cand[nc++] = l;
	if (!nc) return;
	int l = cand[rnd((unsigned)nc)], key = rnd(2) ? 's' : 'r';
	Hunk h = hk[owner[l]];
	char *want = expect(&h, key == 's'), *before = run("cd '%s' && git show :f.txt", repo);
	e.win->line = (size_t)l - 1;
	CHECK(git_leader(&e, 'h', key) == 0);
	settle();
	char *got = key == 's' ? run("cd '%s' && git show :f.txt", repo) : run("cat '%s'", file);
	if (strcmp(got, want)) { fprintf(stderr, "round %d: SPC h %c on line %d (hunk -%ld,%ld +%ld,%ld): not gitsigns' result\n", round, key, l, h.a, h.b, h.c, h.d); fails++; }
	free(got);
	if (key == 's') {
		/* SPC h u: gitsigns' undo_stage_hunk */
		CHECK(git_leader(&e, 'h', 'u') == 0);
		settle();
		got = run("cd '%s' && git show :f.txt", repo);
		if (strcmp(got, before)) { fprintf(stderr, "round %d: SPC h u left the index changed\n", round); fails++; }
		free(got);
	} else reload();
	free(want); free(before);
}

int main(void)
{
	if (getenv("SEED")) rng = strtoull(getenv("SEED"), NULL, 10) | 1;
	setenv("GIT_CONFIG_GLOBAL", "/dev/null", 1); setenv("GIT_CONFIG_NOSYSTEM", "1", 1);
	setenv("GIT_AUTHOR_NAME", "Tester", 1); setenv("GIT_AUTHOR_EMAIL", "t@example.com", 1);
	setenv("GIT_COMMITTER_NAME", "Tester", 1); setenv("GIT_COMMITTER_EMAIL", "t@example.com", 1);
	snprintf(tmp, sizeof tmp, "%s/hgsignsXXXXXX", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
	CHECK(mkdtemp(tmp));
	snprintf(repo, sizeof repo, "%s/repo", tmp);
	snprintf(file, sizeof file, "%s/f.txt", repo);
	for (nline = 0; nline < 40; nline++) { char b[16]; snprintf(b, sizeof b, "line %d", nline + 1); line[nline + 1] = strdup(b); }
	char *o = run("mkdir -p '%s' && cd '%s' && git init -q -b main && git remote add origin https://github.com/someone/proj.git", repo, repo);
	free(o);
	write_file();
	o = run("cd '%s' && git add f.txt && git commit -q -m one", repo);
	free(o);
	CHECK(core_open(&e, file) == 0);
	e.rows = 40; e.cols = 100;
	git_editor(&e);
	git_headless = 1;
	/* never attached: nothing, and no repository looked for (the redraw
	   path does no I/O) */
	CHECK(!git_branch(e.buf) && !git_sign(e.buf, 0) && !git_repos);
	git_attach(e.buf);                     /* as the core on reading the file */
	CHECK(!git_branch(e.buf));             /* before git has looked */
	int rounds = 0, checks = 0;
	for (int round = 0; round < 150; round++, rounds++) {
		edit(round);
		if (rnd(2)) edit(round);
		write_file();
		settle();
		char when[32];
		snprintf(when, sizeof when, "round %d", round);
		compare_signs(when);
		spc_h(round);
		checks++;
		if (rnd(8) == 0) {                /* SPC h S: the whole buffer staged */
			CHECK(git_leader(&e, 'h', 'S') == 0);
			settle();
			o = run("cd '%s' && git diff --name-only", repo);
			if (*o) { fprintf(stderr, "round %d: SPC h S left %s", round, o); fails++; }
			free(o);
		}
	}
	/* SPC g: the file's log and diff (another file committed and changed
	   too); SPC g B the line on GitHub */
	o = run("cd '%s' && echo x > other.txt && git add other.txt && git commit -q -m other other.txt && echo y >> other.txt", repo);
	free(o);
	CHECK(git_leader(&e, 'g', 'L') == 0);
	settle();
	CHECK(git_ha && git_ha->type == T_LOG);
	while (git_ha->busy) CHECK(loop_once(20) == 0);
	o = run("cd '%s' && git log --format=%%h -- f.txt | wc -l", repo);
	int nlog = 0;
	for (int i = 0; i < git_ha->v.nsec; i++) nlog += git_ha->v.sec[i].type == T_COMMIT;
	if (nlog != atoi(o)) { fprintf(stderr, "SPC g L: %d commits, git log -- f.txt %d\n", nlog, atoi(o)); fails++; }
	free(o);
	CHECK(git_leader(&e, 'g', 'd') == 0);
	settle();
	CHECK(git_ha && git_ha->type == T_DIFF);
	while (git_ha->busy) CHECK(loop_once(20) == 0);
	int nfile = 0;
	for (int i = 0; i < git_ha->v.nsec; i++)
		if (git_ha->v.sec[i].type == T_FILE && git_ha->v.sec[git_ha->v.sec[i].parent].type != T_INFO) {
			nfile++;
			if (strcmp(sec_value(&git_ha->v, i), "f.txt")) { fprintf(stderr, "SPC g d shows %s\n", sec_value(&git_ha->v, i)); fails++; }
		}
	o = run("cd '%s' && git diff --name-only -- f.txt | wc -l", repo);
	if (nfile != atoi(o)) { fprintf(stderr, "SPC g d: %d files, git diff -- f.txt %d\n", nfile, atoi(o)); fails++; }
	free(o);
	/* xdg-open: a script on PATH that writes what it was given */
	char bin[PATH_MAX + 8], path[PATH_MAX * 2];
	snprintf(bin, sizeof bin, "%s/bin", tmp);
	o = run("mkdir -p '%s' && printf '#!/bin/sh\\necho \"$1\" > %s/opened\\n' > '%s/xdg-open' && chmod +x '%s/xdg-open'", bin, tmp, bin, bin);
	free(o);
	snprintf(path, sizeof path, "%s:%s", bin, getenv("PATH"));
	setenv("PATH", path, 1);
	e.win->line = 6;
	CHECK(git_leader(&e, 'g', 'B') == 0);
	for (int i = 0; i < 100 && access(strcat(strcpy(path, tmp), "/opened"), F_OK); i++) CHECK(loop_once(20) == 0);
	o = run("cat '%s/opened'", tmp);
	if (strcmp(o, "https://github.com/someone/proj/blob/main/f.txt#L7\n")) { fprintf(stderr, "SPC g B opened %s", o); fails++; }
	free(o);
	branch_check("main");
	o = run("cd '%s' && git checkout -q --detach && git rev-parse HEAD | cut -c1-6 | tr -d '\\n'", repo);
	branch_check(o);
	free(o);
	o = run("cd '%s' && git checkout -q -b feature", repo);
	free(o);
	branch_check("feature");
	o = run("echo x > '%s/out.txt'", tmp);
	free(o);
	snprintf(path, sizeof path, "%s/out.txt", tmp);
	/* the same buffer renamed outside the repository (:saveas) and attached
	   again: the old repository is gone */
	int signs = 0;
	for (int l = 0; l <= nline; l++) signs += git_sign(e.buf, (size_t)l) != 0;
	CHECK(signs > 0);                      /* so the check below can fail */
	char *was = e.buf->path;
	e.buf->path = path;
	git_attach(e.buf);
	CHECK(!git_branch(e.buf));
	for (int l = 0; l <= nline; l++) CHECK(!git_sign(e.buf, (size_t)l));
	e.buf->path = was;
	git_free();
	core_free(&e);
	for (int i = 1; i <= nline; i++) free(line[i]);
	o = run("rm -rf '%s'", tmp);
	free(o);
	if (fails) { fprintf(stderr, "%d differences from gitsigns\n", fails); return 1; }
	printf("test_git_signs: %d rounds of random edits: every line's sign and SPC h s, r, u, S as gitsigns; SPC g L, d, B; the branch as lualine\n", rounds);
	return 0;
}
