/* The status buffer (magit-status-sections-hook without the merge, rebase,
   am, sequencer, bisect and push-remote sections) built from one refresh's
   output, and shown in its core buffer with the cursor where magit's
   magit--refresh-buffer-set-positions puts it. */
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "core.h"
#include "git.h"
#include "gitint.h"
#include "keys.h"
#include "text.h"

static int linef(View *v, const char *a, const char *b, const char *c)
{
	Str t = { 0 };
	int r = str_addz(&t, a) || str_addz(&t, b) || (c && (str_add(&t, " ", 1) || str_addz(&t, c)))
		|| sec_line(v, t.s, t.n) ? -1 : 0;
	str_free(&t);
	return r;
}

/* the subject of the log line whose hash starts the oid */
static int subject_of(const Str *log, const char *oid, Str *out)
{
	out->n = 0;
	for (size_t i = 0; i < log->n; ) {
		const char *p = log->s + i, *e = memchr(p, '\n', log->n - i);
		size_t len = e ? (size_t)(e - p) : log->n - i;
		i += len + 1;
		const char *h = memchr(p, 0x1f, len);
		if (!h) continue;
		h++;
		const char *he = memchr(h, 0x1f, len - (size_t)(h - p));
		if (!he || he == h || strncmp(oid, h, (size_t)(he - h))) continue;
		const char *d = memchr(he + 1, 0x1f, len - (size_t)(he + 1 - p));
		if (!d) continue;
		const char *s = d + 1, *se = memchr(s, 0x1f, len - (size_t)(s - p));
		return str_add(out, s, se ? (size_t)(se - s) : len - (size_t)(s - p));
	}
	return 0;
}

/* Behind only: the log holds @{upstream}'s new commits and HEAD's recent
   ones; the recent ones are those HEAD reaches through their parents. */
static int split_behind(const Str *log, const char *oid, Str *recent, Str *unpulled)
{
	Str reach = { 0 };             /* hashes HEAD reaches, " h " each */
	int r = -1, nrecent = 0;
	for (size_t i = 0; i < log->n; ) {
		const char *p = log->s + i, *e = memchr(p, '\n', log->n - i);
		size_t len = e ? (size_t)(e - p) : log->n - i;
		i += len + 1;
		const char *f[5];
		size_t fl[5];
		int k = 0;
		for (const char *q = p; k < 5; k++) {
			const char *x = memchr(q, 0x1f, len - (size_t)(q - p));
			f[k] = q;
			fl[k] = x ? (size_t)(x - q) : len - (size_t)(q - p);
			if (!x) { k++; break; }
			q = x + 1;
		}
		if (k < 5) continue;
		char key[96];
		snprintf(key, sizeof key, " %.*s ", (int)(fl[1] < 80 ? fl[1] : 80), f[1]);
		int mine = !strncmp(oid, f[1], fl[1]) || (reach.s && strstr(reach.s, key));
		if (mine) {
			/* its parents are HEAD's too */
			for (const char *q = f[4]; q < f[4] + fl[4]; ) {
				const char *sp = memchr(q, ' ', (size_t)(f[4] + fl[4] - q));
				size_t pl = sp ? (size_t)(sp - q) : (size_t)(f[4] + fl[4] - q);
				if (str_add(&reach, " ", 1) || str_add(&reach, q, pl) || str_add(&reach, " ", 1)) goto out;
				q += pl + 1;
			}
			if (nrecent++ < 10 && (str_add(recent, p, len) || str_add(recent, "\n", 1))) goto out;
		} else if (str_add(unpulled, p, len) || str_add(unpulled, "\n", 1)) goto out;
	}
	r = 0;
out:
	str_free(&reach);
	return r;
}

/* git stash list from the reflog file: "old new ident time tz\tmessage",
   oldest first; magit-insert-stashes shows "stash@{N} message" */
static int stashes(Repo *r, View *v)
{
	char p[4096];
	snprintf(p, sizeof p, "%s/logs/refs/stash", r->common);
	FILE *f = fopen(p, "r");
	if (!f) return 0;
	Str all = { 0 };
	char buf[4096];
	size_t n;
	while ((n = fread(buf, 1, sizeof buf, f)) > 0) if (str_add(&all, buf, n)) break;
	fclose(f);
	int count = 0, rc = 0;
	for (size_t i = 0; i < all.n; i++) count += all.s[i] == '\n';
	if (count) {
		int s = sec_open(v, T_STASHES, "refs/stash", 10, 1);
		rc = s < 0 || sec_line(v, "Stashes:", 8);
		/* newest is the last line: walk the lines backwards */
		size_t le = all.n;
		if (le && all.s[le - 1] == '\n') le--;
		for (int k = 0; k < count && !rc; k++) {
			size_t ls = le;
			while (ls && all.s[ls - 1] != '\n') ls--;
			const char *tab = memchr(all.s + ls, '\t', le - ls);
			char name[32];
			int nl = snprintf(name, sizeof name, "stash@{%d}", k);
			Str t = { 0 };
			rc = str_add(&t, name, (size_t)nl) || str_add(&t, " ", 1)
				|| (tab && str_add(&t, tab + 1, le - (size_t)(tab + 1 - all.s)))
				|| sec_open(v, T_STASH, name, (size_t)nl, 0) < 0 || sec_line(v, t.s, t.n);
			str_free(&t);
			if (!rc) sec_close(v);
			le = ls ? ls - 1 : 0;
		}
		if (!rc) rc = sec_line(v, "", 0);
		if (!rc) { sec_close(v); rc = sec_child_count(v, s); }
	}
	str_free(&all);
	return rc ? -1 : 0;
}

static int commits(View *v, int type, const char *head, const char *value, int hide,
                   const char *s, size_t n, int m, const Branch *b)
{
	int i = sec_open(v, type, value, value ? strlen(value) : 0, hide);
	if (i < 0 || sec_line(v, head, strlen(head)) || parse_log(v, s, n, m, b) || sec_line(v, "", 0)) return -1;
	sec_close(v);
	return sec_child_count(v, i);
}

static int diffs(View *v, int type, const char *head, const Str *out, int cached, const Branch *b)
{
	if (!out->n) return 0;
	int i = sec_open(v, type, NULL, 0, 0);
	if (i < 0 || sec_line(v, head, strlen(head))) return -1;
	int files = parse_diff(v, out->s, out->n, 1, cached, b);
	if (files < 0 || sec_line(v, "", 0)) return -1;
	sec_close(v);
	return sec_child_count(v, i);
}

/* the visible line the cursor is on */
static size_t cursor(Repo *r)
{
	Editor *e = git_ed();
	if (git_on(r->bufnr)) return e->win->line;
	return r->cur;
}

/* --- magit-section-goto-successor ------------------------------------------ */

static int same(const View *a, int i, const View *b, int j)
{
	for (; i >= 0 && j >= 0; i = a->sec[i].parent, j = b->sec[j].parent)
		if (a->sec[i].type != b->sec[j].type || strcmp(sec_value(a, i), sec_value(b, j))) return 0;
	return i < 0 && j < 0;
}

static int find(const View *a, int i, const View *b)
{
	for (int j = 0; j < b->nsec; j++) if (same(a, i, b, j)) return j;
	return -1;
}

static int find_top_type(const View *b, int type)
{
	for (int j = 0; j < b->nsec; j++) if (b->sec[j].type == type && b->sec[j].parent == 0) return j;
	return -1;
}

/* magit-section-goto-successor--related-1 */
static int related(const View *a, int i, const View *b)
{
	int t = a->sec[i].type, j;
	int other = t == T_STAGED ? T_UNSTAGED : t == T_UNSTAGED ? T_STAGED
		: t == T_UNPUSHED ? T_UNPULLED : t == T_UNPULLED ? T_UNPUSHED : -1;
	if (other >= 0 && (j = find_top_type(b, other)) >= 0) return j;
	int p = a->sec[i].parent;
	if (p < 0) return -1;
	if (a->sec[i].after < a->sec[p].after && (j = find(a, a->sec[i].after, b)) >= 0) return j;
	int prev = -1;
	for (int k = p + 1; k < i; k = a->sec[k].after) prev = k;
	if (prev >= 0 && (j = find(a, prev, b)) >= 0) return j;
	if ((j = find(a, p, b)) >= 0) return j;
	return related(a, p, b);
}

size_t git_successor(const View *a, size_t vis, const View *b)
{
	if (!a->nvis || !b->nvis) return 0;
	if (vis >= a->nvis) vis = a->nvis - 1;
	size_t l = a->vis[vis];
	int i = sec_at(a, l), j;
	if (i < 0) return 0;
	size_t off = l - a->sec[i].start;
	if ((j = find(a, i, b)) >= 0) {
		size_t t = b->sec[j].start + off;
		if (t >= b->nln || sec_at(b, t) != j) t = b->sec[j].start;
		return sec_vis_of(b, t);
	}
	if (a->sec[i].type == T_HUNK) {
		/* the hunk now in its place, else the file's last hunk */
		int p = find(a, a->sec[i].parent, b);
		if (p >= 0 && p + 1 < b->sec[p].after) {
			int k = 0, last = -1, pick = -1;
			for (int c = a->sec[i].parent + 1; c < i; c = a->sec[c].after) k++;
			for (int c = p + 1, n = 0; c < b->sec[p].after; c = b->sec[c].after, n++) {
				if (n == k) pick = c;
				last = c;
			}
			return sec_vis_of(b, b->sec[pick >= 0 ? pick : last].start);
		}
	}
	if ((j = related(a, i, b)) >= 0) return sec_vis_of(b, b->sec[j].start);
	return vis < b->nvis ? vis : b->nvis - 1;
}

/* "Merging NAME" into t: magit-get-shortname, a local branch at oid, else
   the abbreviated oid */
static void branch_of(Repo *r, const char *oid, Str *t)
{
	char p[PATH_MAX + 32], line[1024], name[1024] = "";
	t->n = 0;
	str_addz(t, "Merging ");
	snprintf(p, sizeof p, "%s/packed-refs", r->common);
	FILE *f = fopen(p, "r");
	while (f && fgets(line, sizeof line, f))
		if (!strncmp(line, oid, 40) && !strncmp(line + 41, "refs/heads/", 11)) {
			line[strcspn(line, "\n")] = 0;
			snprintf(name, sizeof name, "%s", line + 52);
		}
	if (f) fclose(f);
	snprintf(p, sizeof p, "%s/refs/heads", r->common);
	DIR *d = opendir(p);
	struct dirent *de;
	while (d && (de = readdir(d))) {
		if (de->d_name[0] == '.') continue;
		snprintf(p, sizeof p, "%s/refs/heads/%s", r->common, de->d_name);
		FILE *h = fopen(p, "r");
		if (h && fgets(line, sizeof line, h) && !strncmp(line, oid, 40)) snprintf(name, sizeof name, "%s", de->d_name);
		if (h) fclose(h);
	}
	if (d) closedir(d);
	if (name[0]) str_addz(t, name);
	else str_add(t, oid, 7);
}

/* --- build ------------------------------------------------------------------- */

int git_build_status(Repo *r)
{
	size_t was = cursor(r);
	view_free(&r->old);
	r->old = r->st;
	r->st = (View){ .cache = &r->cache };
	View *v = &r->st;
	Branch *b = &r->br;
	Str subj = { 0 }, t = { 0 }, recent = { 0 }, unpulled = { 0 };
	int rc = -1;
	view_clear(v);
	if (sec_open(v, T_STATUS, NULL, 0, 0) < 0) goto out;
	/* magit-insert-status-headers */
	const Str *log = &r->out[SLOT_LOG];
	if (!b->oid || !strcmp(b->oid, "(initial)")) {
		if (sec_line(v, "In the beginning there was darkness", 35) || sec_line(v, "", 0)) goto out;
		goto body;
	}
	if (sec_open(v, T_HEADERS, NULL, 0, 0) < 0) goto out;
	if (subject_of(log, b->oid, &subj)) goto out;
	if (!subj.n && str_addz(&subj, "(no commit message)")) goto out;
	if (b->head) {
		if (linef(v, "Head:     ", b->head, subj.s)) goto out;
	} else {
		char h[16];
		snprintf(h, sizeof h, "%.7s", b->oid);
		if (linef(v, "Head:     ", h, subj.s)) goto out;
	}
	if (b->head && b->upstream) {
		char ref[1024];
		snprintf(ref, sizeof ref, "refs/remotes/%s", b->upstream);
		int found = log_subject(log->s, log->n, ref, &subj);
		if (!found) { snprintf(ref, sizeof ref, "refs/heads/%s", b->upstream); found = log_subject(log->s, log->n, ref, &subj); }
		if (found < 0 || linef(v, "Merge:    ", b->upstream, found ? (subj.n ? subj.s : "(no commit message)") : NULL)) goto out;
	}
	if (!r->merging[0] && !r->rc[SLOT_DESCRIBE] && r->out[SLOT_DESCRIBE].n) {
		/* "TAG-N-gHASH": magit-get-current-tag with its distance */
		Str *d = &r->out[SLOT_DESCRIBE];
		size_t n = d->n;
		while (n && d->s[n - 1] == '\n') n--;
		char *g = NULL, *c = NULL;
		for (size_t k = n; k-- > 0 && !c; ) if (d->s[k] == '-') { if (!g) g = d->s + k; else c = d->s + k; }
		if (c) {
			long cnt = strtol(c + 1, NULL, 10);
			t.n = 0;
			char num[32];
			if (str_addz(&t, "Tag:      ") || str_add(&t, d->s, (size_t)(c - d->s))
			    || (cnt > 0 && str_add(&t, num, (size_t)snprintf(num, sizeof num, " (%ld)", cnt)))
			    || sec_line(v, t.s, t.n)) goto out;
		}
	}
	sec_close(v);
	if (sec_line(v, "", 0)) goto out;
	if (r->merging[0] && !r->rc[SLOT_DESCRIBE]) {
		branch_of(r, r->merging, &t);
		if (str_addz(&t, ":") || commits(v, T_MERGING, t.s, "merge", 0, r->out[SLOT_DESCRIBE].s,
		                                  r->out[SLOT_DESCRIBE].n, 0, b)) goto out;
	}
body:
	if (build_untracked(v, b)
	    || diffs(v, T_UNSTAGED, "Unstaged changes:", &r->out[SLOT_DIFF], 0, b)
	    || diffs(v, T_STAGED, "Staged changes:", &r->out[SLOT_CACHED], 1, b)
	    || stashes(r, v)) goto out;
	char head[1024];
	const char *up = b->upstream;
	switch (r->logmode) {
	case LOG_RECENT:
		if (commits(v, T_RECENT, "Recent commits", NULL, 1, log->s, log->n, 0, b)) goto out;
		break;
	case LOG_LR:
		snprintf(head, sizeof head, "Unmerged into %s:", up);
		if (commits(v, T_UNPUSHED, head, "@{upstream}..", 0, log->s, log->n, '<', b)) goto out;
		if (b->behind) {
			snprintf(head, sizeof head, "Unpulled from %s:", up);
			if (commits(v, T_UNPULLED, head, "..@{upstream}", 1, log->s, log->n, '>', b)) goto out;
		}
		break;
	case LOG_BEHIND:
		if (split_behind(log, b->oid, &recent, &unpulled)
		    || commits(v, T_RECENT, "Recent commits", NULL, 1, recent.s, recent.n, 0, b)) goto out;
		snprintf(head, sizeof head, "Unpulled from %s:", up);
		if (commits(v, T_UNPULLED, head, "..@{upstream}", 1, unpulled.s, unpulled.n, 0, b)) goto out;
		break;
	}
	/* Emacs's point can rest after the last newline: one empty line there */
	if (sec_line(v, "", 0)) goto out;
	sec_close(v);
	if (sec_render(v)) goto out;
	if (!r->old.nln) {
		/* magit-status-goto-initial-section: the section after the first */
		int i = sec_at(v, 0), p = i >= 0 ? v->sec[i].parent : -1;
		r->cur = p >= 0 && v->sec[i].after < v->sec[p].after ? sec_vis_of(v, v->sec[v->sec[i].after].start) : 0;
	} else {
		r->cur = git_successor(&r->old, was, v);
		git_remark(r->bufnr, !git_ha, &r->old, v);
	}
	r->gen++;
	rc = 0;
out:
	str_free(&subj); str_free(&t); str_free(&recent); str_free(&unpulled);
	return rc;
}

/* --- into the core buffer ---------------------------------------------------- */

Buf *git_buf(int nr)
{
	Editor *e = git_ed();
	return e && nr ? core_buf_find(e, nr) : NULL;
}

int git_on(int nr)
{
	Editor *e = git_ed();
	return e && nr && e->buf && e->buf->nr == nr;
}

/* every window on it gets the cursor line cur */
void git_fill(int nr, View *v, size_t cur)
{
	Buf *b = git_buf(nr);
	if (b) core_buf_fill(git_ed(), b, v->text.s, v->text.n, cur);
}

void git_show(Repo *r, View *v)
{
	git_fill(r->bufnr, v, r->cur);
}

/* magit-refresh-buffer deactivates the mark after a command: Visual mode
   ends when a write is queued from one of r's buffers (headless: any) */
void git_unmark(Repo *r)
{
	Editor *e = git_ed();
	int on = git_headless || git_on(r->bufnr);
	for (Aux *a = r->aux; a; a = a->next) on |= git_on(a->bufnr);
	if (e && on) keys_visual(e, 0, 0);
}

/* A view read again for another reason (a change outside, magit leaves
   the buffer as it is): the selection's other end goes where the cursor
   goes (git_successor), so it still selects the same lines */
void git_remark(int nr, int focused, const View *old, const View *v)
{
	Editor *e = git_ed();
	if (e && e->ks.visual && old->nvis && (git_on(nr) || (git_headless && focused)))
		keys_visual(e, e->ks.visual, git_successor(old, e->ks.vline, v));
}
