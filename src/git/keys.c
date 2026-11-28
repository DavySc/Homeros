/* The keys of the git buffers (evil-collection-magit with its defaults),
   magit's prompts and transient popups, and the commands behind them.  Each
   function names the magit command it copies.  A key is looked up as magit
   does: the section at point decides (magit's section keymaps remap
   magit-stage-files, magit-unstage-files, magit-delete-thing and
   magit-visit-thing), else magit-mode-map's binding runs. */
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "core.h"
#include "git.h"
#include "keys.h"
#include "loop.h"
#include "uni.h"
#include "gitint.h"

int git_headless;
static Repo *hr;                   /* headless: the view keys go to */
Aux *git_ha;
#define ha git_ha

typedef struct { Repo *r; Aux *a; View *v; int b; } Foc;     /* b: the buffer's number, 0 headless */

static int focus(Editor *e, Foc *f)
{
	(void)e;                           /* git_on reads the editor's current buffer */
	for (Repo *r = git_repos; r; r = r->next) {
		if (git_on(r->bufnr)) { *f = (Foc){ r, NULL, &r->st, r->bufnr }; return 1; }
		for (Aux *a = r->aux; a; a = a->next)
			if (git_on(a->bufnr)) { *f = (Foc){ r, a, &a->v, a->bufnr }; return 1; }
	}
	if (git_headless && hr) { *f = (Foc){ hr, ha, ha ? &ha->v : &hr->st, 0 }; return 1; }
	return 0;
}

static size_t cur_get(Editor *e, const Foc *f)
{
	return f->b ? e->win->line : f->a ? f->a->cur : f->r->cur;
}

static void cur_set(Editor *e, const Foc *f, size_t vis)
{
	if (f->v->nvis && vis >= f->v->nvis) vis = f->v->nvis - 1;
	if (f->b) core_goto(e, vis, 0);
	else if (f->a) f->a->cur = vis;
	else f->r->cur = vis;
	core_changed = 1;
}

/* the section at point and its line */
static int at(Editor *e, const Foc *f, size_t *line)
{
	size_t vis = cur_get(e, f);
	if (vis >= f->v->nvis) return -1;
	*line = f->v->vis[vis];
	return sec_at(f->v, *line);
}

static void go(Editor *e, const Foc *f, int i) { cur_set(e, f, sec_vis_of(f->v, f->v->sec[i].start)); }

/* shown again after a visibility change, the cursor on line l */
static void reshow(Editor *e, const Foc *f, size_t l)
{
	if (sec_render(f->v)) return;
	size_t vis = sec_vis_of(f->v, l);
	if (f->b) git_fill(f->b, f->v, vis);
	cur_set(e, f, vis);
}

/* --- magit-section motions ------------------------------------------------------ */

static int next_sib(const View *v, int i)
{
	int p = v->sec[i].parent, n = v->sec[i].after;
	return p >= 0 && n < v->sec[p].after ? n : -1;
}

static int prev_sib(const View *v, int i)
{
	int p = v->sec[i].parent, prev = -1;
	if (p >= 0) for (int k = p + 1; k < i; k = v->sec[k].after) prev = k;
	return prev;
}

static int last_child(const View *v, int i)
{
	int c = -1;
	for (int k = i + 1; k < v->sec[i].after; k = v->sec[k].after) c = k;
	return c;
}

/* magit-section-forward */
static void sec_forward(Editor *e, const Foc *f)
{
	const View *v = f->v;
	size_t l, vis = cur_get(e, f);
	int i = at(e, f, &l);
	if (i < 0 || vis + 1 >= v->nvis) { core_msg(e, "No next section"); return; }
	if (v->sec[i].parent < 0) { cur_set(e, f, vis + 1); return; }
	int next = -1;
	if (!v->sec[i].hidden && i + 1 < v->sec[i].after && v->sec[i + 1].start > l) next = i + 1;
	while (i >= 0 && next < 0)
		if ((next = next_sib(v, i)) < 0) i = v->sec[i].parent;
	if (next < 0) core_msg(e, "No next section");
	else go(e, f, next);
}

/* magit-section-backward */
static void sec_backward(Editor *e, const Foc *f)
{
	const View *v = f->v;
	size_t l, vis = cur_get(e, f);
	int i = at(e, f, &l), c;
	if (i < 0 || !vis) { core_msg(e, "No previous section"); return; }
	if (v->sec[i].parent >= 0 && l + 1 == v->sec[i].end && !v->ln[l].len && (c = last_child(v, i)) >= 0) {
		go(e, f, c);
		return;
	}
	if (v->sec[i].parent >= 0 && l != v->sec[i].start) { go(e, f, i); return; }
	int prev = prev_sib(v, i);
	if (prev >= 0)
		while (!v->sec[prev].hidden && (c = last_child(v, prev)) >= 0) prev = c;
	else prev = v->sec[i].parent;
	if (prev >= 0) go(e, f, prev);
	else cur_set(e, f, vis - 1);      /* the root's last line: forward-line -1 */
}

/* magit-section-forward-sibling (gj) and -backward-sibling (gk) */
static void sec_sibling(Editor *e, const Foc *f, int dir)
{
	size_t l, vis = cur_get(e, f);
	int i = at(e, f, &l), s;
	if (i < 0) return;
	if (f->v->sec[i].parent < 0) { if (dir > 0 || vis) cur_set(e, f, dir > 0 ? vis + 1 : vis - 1); }
	else if ((s = dir > 0 ? next_sib(f->v, i) : prev_sib(f->v, i)) >= 0) go(e, f, s);
	else if (dir > 0) sec_forward(e, f);
	else sec_backward(e, f);
}

/* magit-section-toggle */
static void toggle(Editor *e, const Foc *f)
{
	View *v = f->v;
	size_t l;
	int i = at(e, f, &l);
	if (i < 0) return;
	if (v->sec[i].parent < 0) { core_msg(e, "Cannot hide root section"); return; }
	v->sec[i].hidden = !v->sec[i].hidden;
	if (v->sec[i].hidden) l = v->sec[i].start;
	sec_cache_set(v, i);
	reshow(e, f, l);
}

/* magit-jump-to-*: a section of the status buffer */
static void jump(Editor *e, const Foc *f, int type, const char *name)
{
	for (int i = 1; i < f->v->nsec; i = f->v->sec[i].after)
		if (f->v->sec[i].type == type) { go(e, f, i); return; }
	core_msg(e, "Section \"%s\" wasn't found", name);
}

/* --- prompts: y-or-n-p, read-string and transient popups ----------------------- */

typedef void Answer(Editor *e, Repo *r, const char *a);
typedef struct { int key; void (*fn)(Editor *, const Foc *); } Act;
typedef struct { const char *line; Act act[8]; } Pop;
static struct {
	int kind;
	char q[512], in[256];
	size_t n;
	Answer *fn;
	Repo *r;
	const Pop *pop;
	Foc f;
	char arg[256];             /* carried from an earlier prompt */
	unsigned long ver;         /* the version shown when the question was put (Git.tla Ask) */
	int answering;             /* an answer runs: its writes and questions keep ver */
} P;
static int pend;                   /* a key prefix: 'g', or 'Z' in the message buffer */

int git_prompt(void) { return P.kind; }

/* the version of what the focused view shows */
static unsigned long seen(Editor *e, Repo *r)
{
	Foc f;
	return focus(e, &f) && f.r == r && f.a ? f.a->ver : r->shown;
}

static void ask(Editor *e, Repo *r, int kind, Answer *fn, const char *fmt, ...)
{
	if (!P.answering) P.ver = seen(e, r);
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(P.q, sizeof P.q, fmt, ap);
	va_end(ap);
	P.kind = kind; P.fn = fn; P.r = r; P.n = 0; P.in[0] = 0;
	core_msg(e, "%s", P.q);
	core_changed = 1;
}

/* magit-confirm: "...? (y or n) " */
#define confirm(e, r, fn, ...) ask(e, r, P_YN, fn, __VA_ARGS__)

/* The answer acts on what the question was put about (Git.tla ActsOnSeen):
   if the view was read again since, the sections under the cursor may be
   others, so nothing is done; if only the repository moved, the write
   carries the old version and is dropped when it starts */
static void answer(Editor *e, const char *a)
{
	if (seen(e, P.r) != P.ver) { core_msg(e, "git: the repository changed since it was shown; nothing done"); return; }
	P.answering = 1;
	P.fn(e, P.r, a);
	P.answering = 0;
}

static void prompt_key(Editor *e, int k)
{
	core_changed = 1;
	if (P.kind == P_POP) {
		if (k == KEY_ESC || k == 7 || k == 'q') { P.kind = 0; e->msg[0] = 0; return; }
		for (const Act *a = P.pop->act; a->key; a++)
			if (a->key == k) { P.kind = 0; e->msg[0] = 0; a->fn(e, &P.f); return; }
		return;
	}
	if (k == KEY_ESC || k == 7 || k == KEY_CTRL_C) { P.kind = 0; core_msg(e, "Quit"); return; }
	if (P.kind == P_YN) {
		if (k == 'y') { P.kind = 0; core_msg(e, "%sy", P.q); answer(e, "y"); }
		else if (k == 'n' || k == KEY_ENTER || k == 10) { P.kind = 0; core_msg(e, "Abort"); }
		else core_msg(e, "%s [Please answer y or n]", P.q);
		return;
	}
	if (k == KEY_ENTER || k == 10) {
		P.kind = 0;
		e->msg[0] = 0;
		P.in[P.n] = 0;
		answer(e, P.in);
		return;
	}
	if (k == KEY_BS || k == 8) {
		while (P.n && ((unsigned char)P.in[P.n - 1] & 0xc0) == 0x80) P.n--;
		if (P.n) P.n--;
	} else if (k >= 32 && k < 0x110000 && P.n + 4 < sizeof P.in)
		P.n += (size_t)uni_put((unsigned char *)P.in + P.n, (unsigned)k);
	P.in[P.n] = 0;
	core_msg(e, "%s%s", P.q, P.in);
}

/* --- writes -------------------------------------------------------------------- */

static Write W;

static Cmd *wstep(void)
{
	if (W.n >= WSTEPS) return NULL;
	Cmd *c = &W.step[W.n++];
	cmd_git(c, 0);
	return c;
}

/* a step running git with these arguments (NULL-ended), then the paths */
static Cmd *wgit(const char *a, ...)
{
	Cmd *c = wstep();
	if (!c) return NULL;
	va_list ap;
	va_start(ap, a);
	for (const char *s = a; s; s = va_arg(ap, const char *)) cmd_add(c, s);
	va_end(ap);
	return c;
}

static void wpaths(Cmd *c, const Str *paths)
{
	if (c) for (size_t i = 0; i < paths->n; i += strlen(paths->s + i) + 1) cmd_add(c, paths->s + i);
}

/* queue W, acting on what the view showed (Git.tla ActsOnSeen) */
static void wsend(Repo *r, const Foc *f, const char *ok)
{
	snprintf(W.ok, sizeof W.ok, "%s", ok ? ok : "");
	W.ver = f && f->a ? f->a->ver : r->shown;
	git_queue(r, &W);
}

/* --- what is at point ------------------------------------------------------------ */

/* magit-diff-type: the top-level section holding i (status), the buffer's (diff) */
static int dtype(const Foc *f, int i)
{
	if (f->a) return f->a->dtype;
	while (i > 0 && f->v->sec[i].parent > 0) i = f->v->sec[i].parent;
	return i > 0 ? f->v->sec[i].type : -1;
}

static int top(const View *v, int type)
{
	for (int i = 1; i < v->nsec; i = v->sec[i].after) if (v->sec[i].type == type) return i;
	return -1;
}

/* the file section for path under top-level section t, or -1 */
static int file_in(const View *v, int t, const char *path)
{
	if (t < 0) return -1;
	for (int i = t + 1; i < v->sec[t].after; i = v->sec[i].after)
		if (v->sec[i].type == T_FILE && !strcmp(sec_value(v, i), path)) return i;
	return -1;
}

static int line_is(const View *v, size_t l, char c) { return v->ln[l].len && v->pool.s[v->ln[l].off] == c; }

/* "@@ -a,b +c,d @@": a and c */
static void hunk_starts(const View *v, int h, long *a, long *c)
{
	const char *s = v->pool.s + v->ln[v->sec[h].start].off;
	const char *m = strchr(s, '-'), *p = strchr(s, '+');
	*a = m ? strtol(m + 1, NULL, 10) : 0;
	*c = p ? strtol(p + 1, NULL, 10) : 0;
}

/* The lines of a hunk the Visual selection holds (magit-apply-region),
   set by region(); re 0: whole hunks */
static size_t rb, re;

/* The patch for hunks [h, z) of one file: its header and the hunks, the
   first's new start set to its old start (magit-apply--adjust-hunk-new-starts).
   With a selection, magit-diff-hunk-region-patch: a line outside it whose
   sign is op becomes context, other changed lines outside it go; git
   apply --recount then counts the lines (diff-fixup-modifs in magit). */
static int patch(const View *v, int h, int z, char op, Str *p)
{
	int fs = v->sec[h].parent;
	if (str_addz(p, v->pool.s + v->sec[fs].aux)) return -1;
	long off = 0;
	for (int k = h; k < z; k = v->sec[k].after) {
		int pseudo = v->sec[k].type != T_HUNK || !line_is(v, v->sec[k].start, '@');
		for (size_t l = v->sec[k].start; l < v->sec[k].end; l++) {
			const char *s = v->pool.s + v->ln[l].off;
			size_t n = v->ln[l].len;
			if (l == v->sec[k].start && !pseudo) {
				long a, c;
				hunk_starts(v, k, &a, &c);
				if (k == h) off = c - a;
				const char *pl = strchr(s, '+'), *e = pl + 1;
				while (*e >= '0' && *e <= '9') e++;
				char num[32];
				if (str_add(p, s, (size_t)(pl + 1 - s))
				    || str_add(p, num, (size_t)snprintf(num, sizeof num, "%ld", c - off))
				    || str_add(p, e, n - (size_t)(e - s)) || str_add(p, "\n", 1)) return -1;
			} else if (re && (l < rb || l > re) && n && *s != ' ' && *s != '@') {
				if (*s == op && (str_add(p, " ", 1) || str_add(p, s + 1, n - 1) || str_add(p, "\n", 1))) return -1;
			} else if (str_add(p, s, n) || str_add(p, "\n", 1)) return -1;
		}
	}
	return 0;
}

/* git apply args... - with the patch of hunks [h, z) */
static int apply(const View *v, int h, int z, const char *a1, const char *a2)
{
	if (!strncmp(v->pool.s + v->sec[v->sec[h].parent].aux, "diff --cc", 9)) return 1;
	/* --index refuses a file whose stat the index has out of date ("does
	   not match index"); magit's refresh has written it fresh, ours never
	   writes (git_diff), so the write refreshes it first */
	if (a2 && !strcmp(a2, "--index") && !wgit("update-index", "-q", "--unmerged", "--refresh", NULL)) return -1;
	Cmd *c = wgit("apply", NULL);
	if (!c) return -1;
	if (a1) cmd_add(c, a1);
	if (a2) cmd_add(c, a2);
	cmd_add(c, "-p0"); cmd_add(c, "-C3"); cmd_add(c, "--ignore-space-change"); cmd_add(c, "--recount"); cmd_add(c, "-");
	return patch(v, h, z, a1 && !strcmp(a1, "--reverse") ? '+' : '-', &c->in);
}

/* the values of the file sections among siblings [i, z), each with its NUL */
static void values(const View *v, int i, int z, Str *out)
{
	for (; i < z; i = v->sec[i].after)
		if (v->sec[i].type == T_FILE) str_add(out, sec_value(v, i), strlen(sec_value(v, i)) + 1);
}

/* What s, u and x act on: the section at point i, else the Visual
   selection as magit takes it.  When the selection's lines lie in the body
   of one hunk (magit-diff-scope's region), rb and re are set to them (re
   is 0 otherwise).  Else the siblings [*s, *z): the hunks or files from the one whose heading
   the selection starts on to the one whose heading it ends on
   (magit-region-sections), or i alone (a top-level section: its children).
   Only lines count: magit's region ends after the selection's last
   character, on its last line. */
static void region(Editor *e, const Foc *f, int i, int *s, int *z)
{
	const View *v = f->v;
	size_t c = cur_get(e, f), a = e->ks.vline;
	int top = v->sec[i].parent == 0 && v->sec[i].type != T_HUNK && v->sec[i].type != T_FILE;
	*s = i + top; *z = v->sec[i].after; re = 0;
	if (!e->ks.visual || a >= v->nvis || c >= v->nvis) return;
	size_t b = v->vis[a < c ? a : c], l = v->vis[a < c ? c : a];
	int x = sec_at(v, b), y = sec_at(v, l);
	if (x == y && v->sec[x].type == T_HUNK && b > v->sec[x].start) { rb = b; re = l; return; }
	if (x < y && b == v->sec[x].start && l == v->sec[y].start && v->sec[x].parent == v->sec[y].parent
	    && (v->sec[x].type == T_HUNK || v->sec[x].type == T_FILE)) { *s = x; *z = v->sec[y].after; }
}

/* one porcelain v2 entry for path: X and Y (' ' for '.'), and a rename's source */
static int xy(Repo *r, const char *path, char *x, char *y, char *orig, size_t norig)
{
	const Str *o = &r->out[SLOT_STATUS];
	for (size_t i = 0; i < o->n; ) {
		const char *p = o->s + i;
		size_t len = strlen(p);
		i += len + 1;
		int sp = p[0] == '1' ? 8 : p[0] == '2' ? 9 : p[0] == 'u' ? 10 : -1;
		if (sp < 0) continue;
		const char *q = p;
		for (int k = 0; k < sp && q; k++) { q = strchr(q, ' '); if (q) q++; }
		if (!q) continue;
		const char *src = p[0] == '2' && i < o->n ? o->s + i : "";
		if (p[0] == '2') i += strlen(src) + 1;
		if (strcmp(q, path)) continue;
		*x = p[2] == '.' ? ' ' : p[2];
		*y = p[3] == '.' ? ' ' : p[3];
		snprintf(orig, norig, "%s", src);
		return 0;
	}
	return -1;
}

/* --- stage, unstage (magit-apply.el) ---------------------------------------------- */

/* magit-stage-files and magit-unstage-files: comma-separated paths for c */
static void paths(Repo *r, Cmd *c, const char *a)
{
	char buf[256];
	snprintf(buf, sizeof buf, "%s", a);
	for (char *s = strtok(buf, ","); s; s = strtok(NULL, ",")) { while (*s == ' ') s++; if (*s && c) cmd_add(c, s); }
	wsend(r, NULL, NULL);
}

/* magit-unstage-1: git reset, or git rm --cached before the first commit */
static Cmd *unstage_cmd(Repo *r)
{
	int initial = !r->br.oid || !strcmp(r->br.oid, "(initial)");
	return initial ? wgit("rm", "--cached", "--", NULL) : wgit("reset", "HEAD", "--", NULL);
}

static void stage_paths(Editor *e, Repo *r, const char *a) { (void)e; paths(r, wgit("add", "--", NULL), a); }
static void unstage_paths(Editor *e, Repo *r, const char *a) { (void)e; paths(r, unstage_cmd(r), a); }

static void do_stage_modified(Editor *e, Repo *r, const char *a)
{
	(void)e; (void)a;
	wgit("add", "-u", ".", NULL);
	wsend(r, NULL, NULL);
}

/* magit-stage-modified (S) */
static void stage_modified(Editor *e, Repo *r)
{
	if (top(&r->st, T_STAGED) >= 0) confirm(e, r, do_stage_modified, "Stage all changes? (y or n) ");
	else do_stage_modified(e, r, "");
}

static void do_unstage_all(Editor *e, Repo *r, const char *a)
{
	(void)e; (void)a;
	wgit("reset", "HEAD", "--", NULL);
	wsend(r, NULL, NULL);
}

/* magit-unstage-all (U) */
static void unstage_all(Editor *e, Repo *r)
{
	if (top(&r->st, T_STAGED) < 0) { core_msg(e, "Nothing to unstage"); return; }
	if (top(&r->st, T_UNSTAGED) >= 0 || top(&r->st, T_UNTRACKED) >= 0)
		confirm(e, r, do_unstage_all, "Unstage all changes? (y or n) ");
	else do_unstage_all(e, r, "");
}

/* apply()'s rc: a combined diff's hunks are refused (magit-apply-hunk) */
static int resolution(Editor *e, int rc)
{
	if (rc <= 0) return 0;
	git_write_free(&W);
	core_msg(e, "Cannot un-/stage resolution hunks.  Stage the whole file");
	return 1;
}

/* The change s, u and x act on (magit-apply--get-selection): the type t of
   the section at point and its diff type d, the sections [s, z) (region)
   and the values of the files among them (p not NULL); the section at
   point or -1 */
static int pick(Editor *e, const Foc *f, int *t, int *d, int *s, int *z, Str *p)
{
	size_t l;
	int i = at(e, f, &l);
	*t = *d = -1;
	if (i < 0) return i;
	*t = f->v->sec[i].type;
	*d = dtype(f, i);
	region(e, f, i, s, z);
	if (p) values(f->v, *s, *z, p);
	return i;
}

/* magit-stage (s on a change; un 0) and magit-unstage (u; un 1), else
   magit-stage-files and magit-unstage-files */
static void stage(Editor *e, const Foc *f, int un)
{
	View *v = f->v;
	Str p = { 0 }, in = { 0 };
	int t, d, s, z, from = un ? T_STAGED : T_UNSTAGED;
	pick(e, f, &t, &d, &s, &z, &p);
	if (t != T_FILE && t != T_HUNK && t != from && (un || t != T_UNTRACKED))
		ask(e, f->r, P_TEXT, un ? unstage_paths : stage_paths, "[comma-separated list] %stage file,s: ", un ? "Uns" : "S");
	else if (d == T_UNTRACKED && un) core_msg(e, "Cannot unstage untracked changes");
	else if (d == T_UNTRACKED) {
		/* magit-stage-untracked */
		wpaths(wgit("add", "--", NULL), &p);
		wsend(f->r, f, NULL);
	} else if (d == from) {
		if (t == T_HUNK) { if (resolution(e, apply(v, s, z, un ? "--reverse" : "--cached", un ? "--cached" : NULL))) goto out; }
		else if (t == T_FILE) wpaths(un ? unstage_cmd(f->r) : wgit("add", "-u", "--", NULL), &p);
		else { if (un) unstage_all(e, f->r); else stage_modified(e, f->r); goto out; }
		wsend(f->r, f, NULL);
	} else if (d == T_UNSTAGED) {
		/* magit-unstage-intent: files added with --intent-to-add */
		for (size_t k = 0; t == T_FILE && k < p.n; k += strlen(p.s + k) + 1)
			if (file_in(&f->r->st, top(&f->r->st, T_STAGED), p.s + k) >= 0) str_add(&in, p.s + k, strlen(p.s + k) + 1);
		if (in.n) { wpaths(unstage_cmd(f->r), &in); wsend(f->r, f, NULL); }
		else core_msg(e, "Already unstaged");
	} else if (d == T_STAGED) core_msg(e, "Already staged");
	else core_msg(e, "Cannot %sstage committed changes", un ? "un" : "");
out:
	str_free(&p); str_free(&in);
}

/* --- discard (magit-discard, magit-discard-files) ----------------------------------- */

enum { K_DELETE, K_RESURRECT, K_RENAME, K_DISCARD, K_NEW, K_RESOLVE, NK };
static struct {
	Str files[NK];             /* per kind; rename: "orig\0file\0" pairs */
	int kind, type, nfile[NK];
	Foc f;
} D;

static void discard_next(Editor *e, Repo *r);

/* magit-discard-apply for whole files under section t of the view */
static void discard_apply_files(Repo *r, int t, const Str *paths)
{
	const View *v = &r->st;
	for (size_t i = 0; i < paths->n; i += strlen(paths->s + i) + 1) {
		int fs = file_in(v, top(v, t), paths->s + i);
		if (fs < 0 || fs + 1 >= v->sec[fs].after) continue;
		if (t == T_STAGED && file_in(v, top(v, T_UNSTAGED), paths->s + i) >= 0) {
			apply(v, fs + 1, v->sec[fs].after, "--reverse", "--cached");
			apply(v, fs + 1, v->sec[fs].after, "--reverse", "--reject");
		} else apply(v, fs + 1, v->sec[fs].after, "--reverse", t == T_STAGED ? "--index" : NULL);
	}
}

static void discard_yes(Editor *e, Repo *r, const char *a)
{
	(void)a;
	Str *p = &D.files[D.kind];
	switch (D.kind) {
	case K_DELETE:
		/* trashed first (magit-delete-by-moving-to-trash); a staged new file leaves the index too */
		if (D.type == T_STAGED) wpaths(wgit("rm", "--cached", "--force", "--", NULL), p);
		W.trash = 1;
		break;
	case K_RESURRECT:
		wpaths(D.type == T_STAGED ? wgit("reset", "--", NULL) : wgit("checkout", "--", NULL), p);
		break;
	case K_RENAME:
		for (size_t i = 0; i < p->n; ) {
			const char *o = p->s + i, *fl = o + strlen(o) + 1;
			wgit("mv", fl, o, NULL);
			i += strlen(o) + strlen(fl) + 2;
		}
		break;
	case K_DISCARD:
		if (D.type == T_UNSTAGED) wpaths(wgit("checkout", "--", NULL), p);
		else discard_apply_files(r, T_STAGED, p);
		/* fall through: the new files were asked about together */
		if (D.nfile[K_NEW]) {
			wpaths(wgit("add", "--", NULL), &D.files[K_NEW]);
			wpaths(wgit("reset", "--", NULL), &D.files[K_NEW]);
		}
		break;
	}
	D.kind++;
	discard_next(e, r);
}

static void discard_end(Editor *e, Repo *r)
{
	if (W.trash) {
		/* the files to trash ride in the step after the last (write.c) */
		Cmd *t = W.n < WSTEPS ? &W.step[W.n] : NULL;
		if (t) wpaths(t, &D.files[K_DELETE]);
		else W.trash = 0;
	}
	if (W.n || W.trash) wsend(r, &D.f, NULL);
	else git_write_free(&W);
	for (int k = 0; k < NK; k++) str_free(&D.files[k]);
	(void)e;
}

/* ask about the next kind of change (magit-confirm-files), in magit's order */
static void discard_next(Editor *e, Repo *r)
{
	static const int order[] = { K_RESOLVE, K_RESURRECT, K_DELETE, K_RENAME, K_DISCARD };
	for (; D.kind < NK; D.kind++) {
		int k = -1;
		for (int j = 0; j < 5; j++) if (order[j] == D.kind) k = D.kind;
		if (k < 0 || (!D.nfile[k] && !(k == K_DISCARD && D.nfile[K_NEW]))) continue;
		const char *one = D.files[k].s;
		int n = D.nfile[k] + (k == K_DISCARD ? D.nfile[K_NEW] : 0);
		if (k == K_DISCARD && !D.nfile[k]) one = D.files[K_NEW].s;
		if (k == K_RESOLVE) {
			core_msg(e, "Conflicts are resolved in the file's buffer");
			continue;
		}
		if (k == K_RENAME) {
			if (n == 1) confirm(e, r, discard_yes, "Undo rename %s -> %s? (y or n) ", one, one + strlen(one) + 1);
			else confirm(e, r, discard_yes, "Undo %d renames? (y or n) ", n);
			return;
		}
		const char *what = k == K_DELETE ? "Trash" : k == K_RESURRECT ? "Resurrect"
			: D.type == T_STAGED ? "Discard staged changes in" : "Discard unstaged changes in";
		if (n == 1) confirm(e, r, discard_yes, "%s \"%s\"? (y or n) ", what, one);
		else confirm(e, r, discard_yes, "%s %d files? (y or n) ", what, n);
		return;
	}
	discard_end(e, r);
}

static void discard_files(Editor *e, const Foc *f, int type, const Str *paths)
{
	for (int k = 0; k < NK; k++) { D.files[k].n = 0; D.nfile[k] = 0; }
	D.kind = 0; D.type = type; D.f = *f;
	for (size_t i = 0; i < paths->n; i += strlen(paths->s + i) + 1) {
		const char *p = paths->s + i;
		char x = ' ', y = ' ', orig[PATH_MAX] = "";
		int k;
		if (type == T_UNTRACKED || xy(f->r, p, &x, &y, orig, sizeof orig)) k = K_DELETE;
		else if ((x == 'D' && y == 'D') || x == 'U' || y == 'U' || (x == 'A' && y == 'A')) k = K_RESOLVE;
		else if (type == T_STAGED && x == 'M') k = K_DISCARD;
		else if (type == T_UNSTAGED && y == 'M') k = K_DISCARD;
		else if (type == T_STAGED && (x == 'A' || x == 'C') && y == 'M') k = K_NEW;
		else if (type == T_STAGED && (x == 'A' || x == 'C')) k = K_DELETE;
		else if ((type == T_STAGED && x == 'D') || (type == T_UNSTAGED && y == 'D')) k = K_RESURRECT;
		else if (type == T_STAGED && x == 'R') {
			str_add(&D.files[K_RENAME], orig, strlen(orig) + 1);
			k = K_RENAME;
		} else continue;
		str_add(&D.files[k], p, strlen(p) + 1);
		D.nfile[k]++;
	}
	discard_next(e, f->r);
}

static void discard_hunk(Editor *e, Repo *r, const char *a)
{
	(void)a;
	/* magit-discard-apply; the answer comes on the view the question was
	   put on (answer), so the selection is the one asked about */
	const Foc *f = &D.f;
	const View *v = f->v;
	int t, d, s, z, i = pick(e, f, &t, &d, &s, &z, NULL);
	if (t != T_HUNK) return;
	const char *file = sec_value(v, v->sec[i].parent);
	if (d == T_UNSTAGED) apply(v, s, z, "--reverse", NULL);
	else if (file_in(&r->st, top(&r->st, T_UNSTAGED), file) >= 0) {
		apply(v, s, z, "--reverse", "--cached");
		apply(v, s, z, "--reverse", "--reject");
	} else apply(v, s, z, "--reverse", "--index");
	wsend(r, f, NULL);
}

static void stash_drop(Editor *e, const Foc *f);

/* x: magit-discard on a change, magit-stash-drop on a stash */
static void discard(Editor *e, const Foc *f)
{
	View *v = f->v;
	Str p = { 0 };
	int t, d, s, z, n = 0, i = pick(e, f, &t, &d, &s, &z, &p);
	if (t == T_STASH) stash_drop(e, f);
	else if (t != T_FILE && t != T_HUNK && t != T_UNSTAGED && t != T_STAGED && t != T_UNTRACKED)
		core_msg(e, "There is no thing at point that could be deleted");
	else if (d != T_UNSTAGED && d != T_STAGED && d != T_UNTRACKED) core_msg(e, "Cannot discard committed changes");
	else {
		D.f = *f;
		for (int k = s; t == T_HUNK && k < z; k = v->sec[k].after) n++;
		/* magit-discard-files, -region, -hunks, -hunk */
		if (t != T_HUNK) discard_files(e, f, d, &p);
		else if (re) confirm(e, f->r, discard_hunk, "Discard region? (y or n) ");
		else if (n > 1) confirm(e, f->r, discard_hunk, "Discard %d hunks from %s? (y or n) ", n, sec_value(v, v->sec[i].parent));
		else if (line_is(v, v->sec[i].start, '@')) confirm(e, f->r, discard_hunk, "Discard hunk? (y or n) ");
	}
	str_free(&p);
}

/* --- the log and diff buffers -------------------------------------------------- */

void git_aux_free(Aux *a)
{
	if (a->busy) proc_kill(a->pid);
	view_free(&a->v);
	cmd_free(&a->cmd);
	str_free(&a->out);
	if (ha == a) ha = NULL;
	free(a);
}

static void aux_done(Aux *a)
{
	Editor *e = git_ed();
	View old = a->v, *v = &a->v;
	*v = (View){ .cache = old.cache };
	view_clear(v);
	const char *s = a->out.s ? a->out.s : "";
	size_t n = a->out.n;
	int rc = sec_open(v, T_STATUS, NULL, 0, 0) < 0;
	if (!rc && a->type == T_LOG) {
		int w = git_on(a->bufnr) ? e->win->w : e ? e->cols : 80;
		rc = parse_logbuf(v, s, n, &a->r->br, w, (long)time(NULL));
	} else if (!rc) {
		rc = a->dtype == T_COMMIT ? parse_revbuf(v, s, n, &a->r->br)
			: a->dtype == T_STASH ? parse_stashbuf(v, s, n, &a->r->br) : parse_diffbuf(v, s, n, &a->r->br);
	}
	if (!rc) rc = sec_line(v, "", 0);
	if (!rc) sec_close(v);
	if (rc || sec_render(v)) { view_clear(v); view_free(&old); return; }
	size_t cur = git_on(a->bufnr) ? e->win->line : a->cur;
	cur = old.nvis ? git_successor(&old, cur, v) : 0;
	git_remark(a->bufnr, ha == a, &old, v);
	view_free(&old);
	a->cur = cur;
	git_fill(a->bufnr, v, cur);
	core_changed = 1;
}

static void on_aux(void *ctx, int stream, const char *b, size_t n)
{
	if (!git_live(ctx)) return;
	Aux *a = ctx;
	if (stream == 1 && n) str_add(&a->out, b, n);
	else if (stream == 0) {
		a->busy = 0;
		/* a read started before this one ended: its output is not this view's */
		if (a->again) { a->again = 0; git_aux_fill(a); }
		else aux_done(a);
	}
}

int git_aux_shown(const Aux *a)
{
	Editor *e = git_ed();
	if (git_headless) return a == ha;
	for (Win *w = e ? e->wins : NULL; w; w = w->next) if (w->buf && w->buf->nr == a->bufnr) return 1;
	return 0;
}

/* a read, never during a write: the write's end fills it again (write.c finish) */
void git_aux_fill(Aux *a)
{
	char *argv[CMD_MAX];
	if (a->busy) { a->again = 1; return; }
	if (a->r->busy == BUSY_WRITE) return;
	char *p = a->cmd.args.s;
	for (int i = 0; i < a->cmd.argc; i++, p += strlen(p) + 1) argv[i] = p;
	argv[a->cmd.argc] = NULL;
	git_drain(a->r);
	a->ver = a->r->ver;
	a->out.n = 0;
	a->pid = proc_spawn(argv, a->r->top, on_aux, a);
	a->busy = a->pid >= 0;
	git_schedule(a->r);                /* the drain may have found changes */
}

/* magit-log-setup-buffer, magit-diff-setup-buffer: one buffer of each kind
   per repository; a diff opens in a window above (magit-display-buffer-traditional) */
static void aux_open(Editor *e, Repo *r, int type, int dt, Cmd *c)
{
	Aux *a = NULL;
	for (Aux *x = r->aux; x; x = x->next) if (x->type == type) a = x;
	if (!a) {
		if (!(a = calloc(1, sizeof *a))) { cmd_free(c); return; }
		a->r = r; a->type = type; a->pid = -1;
		a->next = r->aux; r->aux = a;
	}
	cmd_free(&a->cmd);
	a->cmd = *c;
	a->dtype = dt;
	a->cur = 0;
	if (git_headless) { hr = r; ha = a; }
	else if (git_buf(a->bufnr)) {
		if (!git_on(a->bufnr) && core_buf_goto(e, a->bufnr)) return;
	} else {
		int split = type == T_DIFF && !core_split(e, 0, 0);
		if (split) core_win_goto(e, 'k', 1);
		if (core_edit_file(e, "", 0)) { if (split) core_close(e, 1); return; }
		a->bufnr = e->buf->nr;
		a->split = split;
	}
	git_aux_fill(a);
}

static void log_open(Editor *e, Repo *r, const char *rev, const char *rev2, const char *file)
{
	Cmd c;
	cmd_git(&c, 1);
	cmd_add(&c, "log"); cmd_add(&c, "--format=%h%x1f%D%x1f%aN%x1f%at%x1f%s");
	cmd_add(&c, "--graph"); cmd_add(&c, "--decorate=full"); cmd_add(&c, "-n256");
	cmd_add(&c, rev);
	if (rev2) cmd_add(&c, rev2);
	cmd_add(&c, "--");
	if (file) cmd_add(&c, file);
	aux_open(e, r, T_LOG, -1, &c);
}

/* git diff (magit-diff-unstaged, -staged, -working-tree), one file or all */
static void diff_open(Editor *e, Repo *r, int dt, const char *file)
{
	Cmd c;
	cmd_git(&c, 1);
	git_diff(&c, dt);
	cmd_add(&c, "--stat"); cmd_add(&c, "--no-prefix"); cmd_add(&c, "--ita-visible-in-index");
	cmd_add(&c, "--no-ext-diff"); cmd_add(&c, "--");
	if (file) cmd_add(&c, file);
	aux_open(e, r, T_DIFF, dt, &c);
}

/* magit-show-commit (magit-revision-sections-hook) and magit-stash-show
   (magit-stash-sections-hook): one shell runs their git commands, the parts
   NUL-ended (sec.c parse_revbuf, parse_stashbuf) */
/* A shell script with one argument ($1), run by git's own shell as an
   alias: Windows has no sh on PATH, only git's */
static void cmd_sh(Cmd *c, const char *script, const char *a)
{
	char s[2048];
	snprintf(s, sizeof s, "alias.homeros=!s() { %s; }; s", script);
	cmd_add(c, "git"); cmd_add(c, "-c"); cmd_add(c, s); cmd_add(c, "homeros"); cmd_add(c, a);
}

#define G "g() { git --no-pager --no-optional-locks -c color.ui=false -c diff.noPrefix=false \"$@\"; }; "
static void show_open(Editor *e, Repo *r, const char *rev, int stash)
{
	Cmd c = { 0 };
	cmd_sh(&c, stash ? G "g diff -p --no-prefix \"$1^2..$1\" --; printf '\\0'; "
	                    "g diff -p --no-prefix \"$1^..$1^2\" --; printf '\\0'; "
	                    "! g rev-parse -q --verify \"$1^3\" >/dev/null || g show --format= -p --no-prefix \"$1^3\""
	                  : G "g show -s --decorate=full --format=format:'%D%x00%H%x00Author:     %aN <%aE>%nAuthorDate: %ad%n"
	                    "Commit:     %cN <%cE>%nCommitDate: %cd%n%n%x00%B%x00' \"$1\" && "
	                    "p=$(g rev-parse \"$1^@\") && { [ -z \"$p\" ] || g show -s --format='%h %s' $p; } && printf '\\0' && "
	                    "g branch --merged \"$1\" && printf '\\0' && g branch --contains \"$1\" && printf '\\0' && "
	                    "g show --format= -p --stat --no-prefix --no-ext-diff \"$1\" --", rev);
	aux_open(e, r, T_DIFF, stash ? T_STASH : T_COMMIT, &c);
}

/* magit-mode-bury-buffer */
static void bury(Editor *e, const Foc *f)
{
	if (git_headless) {
		if (f->a) ha = NULL; else hr = NULL;
		core_changed = 1;
		return;
	}
	if (f->a && f->a->split) { f->a->split = 0; core_close(e, 0); }
	else if (f->a && git_buf(f->r->bufnr)) core_buf_goto(e, f->r->bufnr);
	else core_buf_alt(e);
}

/* --- visiting ------------------------------------------------------------------- */

/* magit-diff-visit-file's target: the file of the section at visible line
   vis and its line there (magit-diff-hunk-line); 0, or -1 if no file */
int git_visit_target(const View *v, size_t vis, int dt, char *rel, size_t nrel, size_t *line)
{
	(void)dt;
	if (vis >= v->nvis) return -1;
	size_t l = v->vis[vis];
	int i = sec_at(v, l);
	if (i < 0 || (v->sec[i].type != T_FILE && v->sec[i].type != T_HUNK)) return -1;
	int h = i, fs = i;
	if (v->sec[i].type == T_FILE) h = i + 1 < v->sec[i].after ? i + 1 : -1;
	else fs = v->sec[i].parent;
	snprintf(rel, nrel, "%s", sec_value(v, fs));
	size_t n = strlen(rel);
	if (n && rel[n - 1] == '/') rel[n - 1] = 0;
	*line = 0;
	if (h < 0 || !line_is(v, v->sec[h].start, '@')) return 0;
	long a, c;
	hunk_starts(v, h, &a, &c);
	size_t t = l, s = v->sec[h].start + 1;
	if (t < s || h != i) {
		/* on a heading: the first changed line */
		for (t = s; t < v->sec[h].end && !line_is(v, t, '-') && !line_is(v, t, '+'); t++) {}
	}
	long off = 0;
	for (size_t k = s; k < t; k++) off += !line_is(v, k, '-');
	*line = c + off > 0 ? (size_t)(c + off - 1) : 0;
	return 0;
}

static void visit(Editor *e, const Foc *f)
{
	View *v = f->v;
	size_t l;
	int i = at(e, f, &l), t = i >= 0 ? v->sec[i].type : -1;
	if (t == T_UNSTAGED || t == T_STAGED) { diff_open(e, f->r, t, NULL); return; }
	if (t == T_COMMIT) { show_open(e, f->r, sec_value(v, i), 0); return; }
	if (t == T_STASH) { show_open(e, f->r, sec_value(v, i), 1); return; }
	char rel[PATH_MAX], path[PATH_MAX * 2];
	size_t line;
	if (git_visit_target(v, cur_get(e, f), dtype(f, i), rel, sizeof rel, &line)) {
		core_msg(e, "There is no thing at point that could be visited");
		return;
	}
	snprintf(path, sizeof path, "%s/%s", f->r->top, rel);
	if (!core_edit_file(e, path, 0)) core_goto(e, line, 0);
}

/* --- branches ------------------------------------------------------------------- */

/* magit-get-previous-branch: the last branch HEAD left that is not the current one */
static int prev_branch(Repo *r, char *out, size_t n)
{
	char p[PATH_MAX];
	snprintf(p, sizeof p, "%s/logs/HEAD", r->gitdir);
	FILE *fp = fopen(p, "r");
	if (!fp) return -1;
	char line[2048];
	int found = -1;
	while (fgets(line, sizeof line, fp)) {
		char *m = strstr(line, "\tcheckout: moving from ");
		if (!m) continue;
		m += 23;
		char *to = strstr(m, " to ");
		if (!to) continue;
		*to = 0;
		size_t k = strlen(m);
		int hex = k >= 7;
		for (size_t j = 0; j < k && hex; j++) hex = (m[j] >= '0' && m[j] <= '9') || (m[j] >= 'a' && m[j] <= 'f');
		if (hex || (r->br.head && !strcmp(m, r->br.head))) continue;
		if (strlen(m) < n) { memcpy(out, m, strlen(m) + 1); found = 0; }
	}
	fclose(fp);
	return found;
}

/* magit-branch-or-commit-at-point: a commit's hash */
static const char *at_commit(Editor *e, const Foc *f)
{
	size_t l;
	int i = at(e, f, &l);
	return i >= 0 && f->v->sec[i].type == T_COMMIT ? sec_value(f->v, i) : NULL;
}

static void checkout_go(Editor *e, Repo *r, const char *a)
{
	(void)e;
	const char *x = *a ? a : P.arg;
	if (!*x) return;
	wgit("checkout", x, NULL);
	wsend(r, NULL, "Git finished");
}

/* magit-checkout (b b) */
static void branch_checkout(Editor *e, const Foc *f)
{
	const char *c = at_commit(e, f);
	P.arg[0] = 0;
	if (c) snprintf(P.arg, sizeof P.arg, "%s", c);
	else prev_branch(f->r, P.arg, sizeof P.arg);
	if (P.arg[0]) ask(e, f->r, P_TEXT, checkout_go, "Checkout (default %s): ", P.arg);
	else ask(e, f->r, P_TEXT, checkout_go, "Checkout: ");
}

static void branch_name(Editor *e, Repo *r, const char *a);
static int branch_and_checkout;

static void branch_start(Editor *e, Repo *r, const char *a)
{
	if (*a) snprintf(P.arg, sizeof P.arg, "%s", a);
	ask(e, r, P_TEXT, branch_name, "Name for new branch: ");
}

static void branch_name(Editor *e, Repo *r, const char *a)
{
	(void)e;
	if (!*a) return;
	if (branch_and_checkout) wgit("checkout", "-b", a, P.arg, NULL);
	else wgit("branch", a, P.arg, NULL);
	wsend(r, NULL, "Git finished");
}

/* magit-branch-and-checkout (b c), magit-branch-create (b n) */
static void branch_create(Editor *e, const Foc *f, int co)
{
	const char *c = at_commit(e, f);
	branch_and_checkout = co;
	snprintf(P.arg, sizeof P.arg, "%s", c ? c : f->r->br.head ? f->r->br.head : "HEAD");
	ask(e, f->r, P_TEXT, branch_start, "%s starting at (default %s): ",
	    co ? "Create and checkout branch" : "Create branch", P.arg);
}
static void branch_c(Editor *e, const Foc *f) { branch_create(e, f, 1); }
static void branch_n(Editor *e, const Foc *f) { branch_create(e, f, 0); }

static void delete_go(Editor *e, Repo *r, const char *a)
{
	(void)e;
	if (!*a) return;
	wgit("branch", "-d", a, NULL);
	wsend(r, NULL, NULL);
}

/* magit-branch-delete (b x) */
static void branch_x(Editor *e, const Foc *f) { ask(e, f->r, P_TEXT, delete_go, "Delete branch: "); }

/* --- stashes -------------------------------------------------------------------- */

/* stash@{k}'s message and its commit, from the reflog; 0, or -1 if none */
static int stash_entry(Repo *r, int k, char *msg, size_t nmsg, char *oid)
{
	char p[PATH_MAX];
	snprintf(p, sizeof p, "%s/logs/refs/stash", r->common);
	FILE *fp = fopen(p, "r");
	if (!fp) return -1;
	char line[4096];
	int n = 0;
	while (fgets(line, sizeof line, fp)) n++;
	rewind(fp);
	int want = n - 1 - k, i = 0, rc = -1;
	while (want >= 0 && fgets(line, sizeof line, fp))
		if (i++ == want) {
			line[strcspn(line, "\n")] = 0;
			char *tab = strchr(line, '\t');
			snprintf(msg, nmsg, "%s", tab ? tab + 1 : "");
			snprintf(oid, 8, "%.7s", line + 41);
			rc = 0;
			break;
		}
	fclose(fp);
	return rc;
}

static void stash_go(Editor *e, Repo *r, const char *a)
{
	(void)e;
	/* magit-stash-save with the message as given: git stash push would
	   prefix it with "On branch: " */
	Cmd *c = wstep();
	if (!c) return;
	cmd_free(c);
	/* git stash create's commits, the stash's message replaced by m */
	cmd_sh(c, "m=$1; [ -n \"$m\" ] || m=\"On $(git symbolic-ref -q --short HEAD || echo '(no branch)'): "
	           "$(git log -1 --format='%h %s')\"; h=$(git stash create) && [ -n \"$h\" ] "
	           "|| { echo 'No local changes to save' >&2; exit 1; }; "
	           "c=$(git commit-tree -p \"$h^1\" -p \"$h^2\" -m \"$m\" \"$h^{tree}\") "
	           "&& git stash store -m \"$m\" \"$c\" && git reset -q --hard", a);
	wsend(r, NULL, NULL);
}

/* magit-stash-both (z z) */
static void stash_z(Editor *e, const Foc *f)
{
	ask(e, f->r, P_TEXT, stash_go, "Stash message (default: On\xe2\x80\xa6:\xe2\x80\xa6): ");
}

static const char *stash_verb;
static char stash_oid[8];

static void stash_use(Editor *e, Repo *r, const char *a)
{
	(void)e;
	char name[256];
	if (*a) sscanf(a, "%255s", name); else snprintf(name, sizeof name, "%s", P.arg);
	if (!strcmp(stash_verb, "Drop")) {
		char ok[sizeof W.ok];
		snprintf(ok, sizeof ok, "Deleted refs/%.60s (was %s)", name, stash_oid);
		wgit("stash", "drop", "-q", name, NULL);
		wsend(r, NULL, ok);
	} else {
		wgit("stash", !strcmp(stash_verb, "Pop") ? "pop" : "apply", "--index", "-q", name, NULL);
		wsend(r, NULL, NULL);
	}
}

/* magit-stash-pop (z p), -apply (z a), -drop (z k, x on a stash) */
static void stash_pick(Editor *e, const Foc *f, const char *verb)
{
	size_t l;
	int i = at(e, f, &l), k = 0;
	if (i >= 0 && f->v->sec[i].type == T_STASH) sscanf(sec_value(f->v, i), "stash@{%d}", &k);
	char msg[512];
	if (stash_entry(f->r, k, msg, sizeof msg, stash_oid)) { core_msg(e, "No stash found"); return; }
	stash_verb = verb;
	snprintf(P.arg, sizeof P.arg, "stash@{%d}", k);
	ask(e, f->r, P_TEXT, stash_use, "%s stash (default %s %s): ", verb, P.arg, msg);
}
static void stash_p(Editor *e, const Foc *f) { stash_pick(e, f, "Pop"); }
static void stash_a(Editor *e, const Foc *f) { stash_pick(e, f, "Apply"); }
static void stash_drop(Editor *e, const Foc *f) { stash_pick(e, f, "Drop"); }

/* --- commit, fetch, pull, push ------------------------------------------------------ */

static void commit_run(const Foc *f, int editor, const char *a1, const char *a2, const char *a3)
{
	Cmd *c = &W.step[W.n++];
	if (editor) cmd_editor(c); else cmd_git(c, 0);
	cmd_add(c, "commit");
	if (a1) cmd_add(c, a1);
	if (a2) cmd_add(c, a2);
	if (a3) cmd_add(c, a3);
	W.commit = editor;
	wsend(f->r, f, "Git finished");
}
/* magit-commit-create, -extend, -amend, -reword */
static void commit_c(Editor *e, const Foc *f) { (void)e; commit_run(f, 1, "--verbose", NULL, NULL); }
static void commit_e(Editor *e, const Foc *f) { (void)e; commit_run(f, 0, "--amend", "--no-edit", NULL); }
static void commit_a(Editor *e, const Foc *f) { (void)e; commit_run(f, 1, "--amend", "--verbose", NULL); }
static void commit_w(Editor *e, const Foc *f) { (void)e; commit_run(f, 1, "--amend", "--only", "--verbose"); }

static void remote_run(Editor *e, const Foc *f, const char *a1, const char *a2)
{
	if (!f->r->br.upstream && strcmp(a1, "fetch")) {
		core_msg(e, "No upstream is configured for %s", f->r->br.head ? f->r->br.head : "HEAD");
		return;
	}
	wgit(a1, a2, NULL);
	wsend(f->r, f, "Git finished");
}
/* magit-fetch-from-upstream, -all; magit-pull-from-upstream; magit-push-current-to-upstream */
static void fetch_u(Editor *e, const Foc *f) { remote_run(e, f, "fetch", NULL); }
static void fetch_a(Editor *e, const Foc *f) { remote_run(e, f, "fetch", "--all"); }
static void pull_u(Editor *e, const Foc *f) { remote_run(e, f, "pull", NULL); }
static void push_u(Editor *e, const Foc *f) { remote_run(e, f, "push", "-v"); }

/* magit-log-current, -all-branches, -branches, -all */
static void log_l(Editor *e, const Foc *f) { log_open(e, f->r, "HEAD", NULL, NULL); }
static void log_a(Editor *e, const Foc *f) { log_open(e, f->r, "--all", NULL, NULL); }
static void log_b(Editor *e, const Foc *f) { log_open(e, f->r, "--branches", "--remotes", NULL); }
static void log_L(Editor *e, const Foc *f) { log_open(e, f->r, "--branches", NULL, NULL); }

/* magit-diff-unstaged, -staged, -working-tree, -dwim */
static void diff_u(Editor *e, const Foc *f) { diff_open(e, f->r, T_UNSTAGED, NULL); }
static void diff_s(Editor *e, const Foc *f) { diff_open(e, f->r, T_STAGED, NULL); }
static void diff_w(Editor *e, const Foc *f) { diff_open(e, f->r, -1, NULL); }
static void diff_d(Editor *e, const Foc *f)
{
	size_t l;
	int i = at(e, f, &l), d = i >= 0 ? dtype(f, i) : -1;
	if (i >= 0 && f->v->sec[i].type == T_COMMIT) show_open(e, f->r, sec_value(f->v, i), 0);
	else diff_open(e, f->r, d == T_STAGED ? T_STAGED : T_UNSTAGED, NULL);
}

/* the transients, with evil-collection's keys; a line in the message area */
static const Pop COMMIT = { "Commit: c commit  e extend  a amend  w reword",
	{ { 'c', commit_c }, { 'e', commit_e }, { 'a', commit_a }, { 'w', commit_w } } };
static const Pop BRANCH = { "Branch: b checkout  c new and checkout  n new  x delete",
	{ { 'b', branch_checkout }, { 'c', branch_c }, { 'n', branch_n }, { 'x', branch_x } } };
static const Pop STASH = { "Stash: z both  p pop  a apply  k drop",
	{ { 'z', stash_z }, { 'p', stash_p }, { 'a', stash_a }, { 'k', stash_drop } } };
static const Pop LOG = { "Log: l current  a all references  b all branches  L local branches",
	{ { 'l', log_l }, { 'a', log_a }, { 'b', log_b }, { 'L', log_L } } };
static const Pop DIFF = { "Diff: d dwim  u unstaged  s staged  w worktree",
	{ { 'd', diff_d }, { 'u', diff_u }, { 's', diff_s }, { 'w', diff_w } } };
static const Pop FETCH = { "Fetch: u upstream  a all remotes", { { 'u', fetch_u }, { 'a', fetch_a } } };
static const Pop PULL = { "Pull: u upstream", { { 'u', pull_u } } };
static const Pop PUSH = { "Push: u upstream", { { 'u', push_u } } };

static void popup(Editor *e, const Foc *f, const Pop *p)
{
	P.kind = P_POP; P.pop = p; P.f = *f; P.r = f->r;
	core_msg(e, "%s", p->line);
	core_changed = 1;
}

/* --- keys ----------------------------------------------------------------------- */

/* keys evil-collection leaves to evil: motions, search, ex, visual, windows */
static int vim_key(int k)
{
	return (k >= '0' && k <= '9') || strchr("jkGH ML/?nN:vVy$^", k) != NULL
		|| k == 4 || k == 21 || k == 6 || k == 2 || k == 5 || k == 25 || k == 23 || k == 15
		|| k == KEY_ESC || k >= KEY_UP;
}

/* magit-refresh (g r) */
static void refresh(const Foc *f)
{
	git_unmark(f->r);
	if (f->a) git_aux_fill(f->a);
	f->r->dirty = 1;
	git_schedule(f->r);
}

static int g_key(Editor *e, const Foc *f, int k)
{
	switch (k) {
	case 'g': cur_set(e, f, 0); break;                    /* evil-goto-first-line */
	case 'j': sec_sibling(e, f, 1); break;
	case 'k': sec_sibling(e, f, -1); break;
	case 'h': {                                           /* magit-section-up */
		size_t l;
		int i = at(e, f, &l);
		if (i >= 0 && f->v->sec[i].parent >= 0) go(e, f, f->v->sec[i].parent);
		else core_msg(e, "No parent section");
		break;
	}
	case 'r': case 'R': refresh(f); break;
	case 'z': jump(e, f, T_STASHES, "Stashes"); break;
	case 'n': jump(e, f, T_UNTRACKED, "Untracked files"); break;
	case 'u': jump(e, f, T_UNSTAGED, "Unstaged changes"); break;
	case 's': jump(e, f, T_STAGED, "Staged changes"); break;
	case 't': core_msg(e, "Section \"Tracked files\" wasn't found"); break;
	}
	return 1;
}

int git_key(Editor *e, int k)
{
	git_editor(e);
	if (P.kind) { prompt_key(e, k); return 1; }
	for (Repo *r = git_repos; r; r = r->next) {
		/* with-editor: a message buffer killed otherwise than by ZZ or ZQ cancels */
		if (r->editing == 2 && r->msgnr && !git_buf(r->msgnr)) git_editor_done(r, 0);
		if (git_on(r->msgnr) && r->editing == 2) {
			/* with-editor-finish (ZZ) and -cancel (ZQ) */
			if (pend == 'Z') {
				pend = 0;
				if (k == 'Z' || k == 'Q') git_editor_done(r, k == 'Z');
				return 1;
			}
			if (k != 'Z' || e->mode != MODE_NORMAL) return 0;
			pend = 'Z';
			return 1;
		}
	}
	Foc f;
	if (!focus(e, &f) || e->mode != MODE_NORMAL) return 0;
	if (pend) { pend = 0; return g_key(e, &f, k); }
	switch (k) {
	case 'g': pend = 'g'; return 1;
	case KEY_TAB: toggle(e, &f); return 1;
	case 10: sec_forward(e, &f); return 1;
	case 11: sec_backward(e, &f); return 1;
	case KEY_ENTER: visit(e, &f); return 1;
	case 's': stage(e, &f, 0); return 1;
	case 'S': stage_modified(e, f.r); return 1;
	case 'u': stage(e, &f, 1); return 1;
	case 'U': unstage_all(e, f.r); return 1;
	case 'x': discard(e, &f); return 1;
	case 'q': bury(e, &f); return 1;
	case 'c': popup(e, &f, &COMMIT); return 1;
	case 'b': popup(e, &f, &BRANCH); return 1;
	case 'z': popup(e, &f, &STASH); return 1;
	case 'l': popup(e, &f, &LOG); return 1;
	case 'd': popup(e, &f, &DIFF); return 1;
	case 'f': popup(e, &f, &FETCH); return 1;
	case 'F': popup(e, &f, &PULL); return 1;
	case 'p': popup(e, &f, &PUSH); return 1;
	}
	return !vim_key(k);
}

/* --- SPC g and SPC h, gutter signs ------------------------------------------------ */

/* the repository of a file buffer and the file's path in it, remembered per buffer */
typedef struct { int nr; Repo *r; char *rel; } Of;
static Of *ofs;
static size_t nofs;

static Of *find(const Buf *b)
{
	for (size_t i = 0; i < nofs; i++) if (ofs[i].nr == b->nr) return &ofs[i];
	return NULL;
}

static int scnr;                   /* git_sign's cache (below) */

/* gitsigns' attach on BufRead: the buffer's repository is found here, once
   per file read, so git_sign and git_branch only look it up.  A buffer
   attached again (another file, a new name) is attached anew. */
void git_attach(const Buf *b)
{
	/* deleted buffers; before git has the editor (files read before the
	   first key) none can be told from a live one */
	for (size_t i = 0; git_ed() && i < nofs; i++)
		if (!git_buf(ofs[i].nr)) { free(ofs[i].rel); ofs[i--] = ofs[--nofs]; }
	Of *o = find(b);
	if (o) free(o->rel);
	else {
		Of *n = realloc(ofs, (nofs + 1) * sizeof *n);
		if (!n) return;
		ofs = n;
		o = &ofs[nofs++];
	}
	*o = (Of){ b->nr, NULL, NULL };
	scnr = 0;
	char abs[PATH_MAX], dir[PATH_MAX];
	if (!b->path) return;
	if (git_real(b->path, abs)) {
		/* a file not made yet (:e new): its directory resolved, the name as
		   typed, as file_full; lualine's branch looks from there too */
		const char *n = b->path + strlen(b->path);
		while (n > b->path && n[-1] != '/' && n[-1] != '\\') n--;
		snprintf(dir, sizeof dir, "%.*s", (int)(n - b->path), b->path);
		size_t k;
		if (git_real(*dir ? dir : ".", abs) || (k = strlen(abs)) + 1 + strlen(n) >= sizeof abs) return;
		snprintf(abs + k, sizeof abs - k, "/%s", n);
	}
	snprintf(dir, sizeof dir, "%s", abs);
	char *s = strrchr(dir, '/');
	if (s) *s = 0;
	if (!(o->r = git_repo(dir))) return;
	size_t tl = strlen(o->r->top);
	o->rel = strdup(abs + tl + (abs[tl] == '/'));
	git_schedule(o->r);
}

/* the keys' lookup: a buffer the core has not attached is attached now */
static Of *of(const Buf *b)
{
	if (!find(b)) git_attach(b);
	return find(b);
}

/* the current buffer's file, if it is in a repository; else says so */
static Of *tracked(Editor *e)
{
	Of *o = e->buf ? of(e->buf) : NULL;
	if (!o || !o->r) core_msg(e, "Not in a Git repository");
	return o && o->r ? o : NULL;
}

/* gitsigns' hunks are the diff's blocks of changed lines, without context:
   walk the file's hunks for the block that marks line t (from 1).  A block
   of k removed and m added lines marks min(k, m) lines changed and the rest
   added; removed lines with nothing added in their place mark the line
   above them (line 1 when at the top).  Fills where the block is: its
   lines [k0, k1) in v, and the old and new lines before it. */
typedef struct { size_t k0, k1; long o, n; } Blk;
static int block_at(const View *v, int fs, long t, Blk *bk)
{
	for (int h = fs + 1; h < v->sec[fs].after; h = v->sec[h].after) {
		if (!line_is(v, v->sec[h].start, '@')) continue;
		/* "-a,b +c,d": a count of 0 means the lines go after line a (or c) */
		const char *hd = v->pool.s + v->ln[v->sec[h].start].off;
		char *q;
		long o = strtol(strchr(hd, '-') + 1, &q, 10);
		if (*q == ',' && !strtol(q + 1, NULL, 10)) o++;
		long n = strtol(strchr(q, '+') + 1, &q, 10), del = 0;
		if (*q == ',' && !strtol(q + 1, NULL, 10)) n++;
		int in = 0, hit = 0;
		for (size_t k = v->sec[h].start + 1; k <= v->sec[h].end; k++) {
			char c = k < v->sec[h].end && v->ln[k].len ? v->pool.s[v->ln[k].off] : ' ';
			if (c == '\\' && !in) continue;
			if (c == '-' || c == '+' || c == '\\') {
				if (!in) { in = 1; bk->k0 = k; bk->o = o - 1; bk->n = n - 1; del = 0; }
				if (c == '-') { del++; o++; }
				else if (c == '+') { if (n == t) hit = del ? '~' : '+'; if (del) del--; n++; }
				continue;
			}
			/* context, or the hunk's end */
			if (in && !hit && del && (n - 1 == t || (n == 1 && t == 1))) hit = '_';
			if (in && hit) { bk->k1 = k; return hit; }
			in = 0;
			o++; n++;
		}
	}
	return 0;
}

/* the patch of one block: the file's header, "@@ -o,k +n,m @@" and its
   lines, both starts counted on the side it applies to (the index's for
   staging, the work tree's for a reset): with no context git apply places
   an empty side by the other side's start (measured: a reset of a deletion
   below another landed as many lines lower as the other removed) */
static int block_patch(const View *v, int fs, const Blk *bk, int index, Str *p)
{
	long k = 0, m = 0;
	for (size_t l = bk->k0; l < bk->k1; l++) { k += line_is(v, l, '-'); m += line_is(v, l, '+'); }
	long b = index ? bk->o : bk->n;
	char h[96];
	if (str_addz(p, v->pool.s + v->sec[fs].aux)
	    || str_add(p, h, (size_t)snprintf(h, sizeof h, "@@ -%ld,%ld +%ld,%ld @@\n", b + (k > 0), k, b + (m > 0), m)))
		return -1;
	for (size_t l = bk->k0; l < bk->k1; l++)
		if (str_add(p, v->pool.s + v->ln[l].off, v->ln[l].len) || str_add(p, "\n", 1)) return -1;
	return 0;
}

static Str staged_last;            /* gitsigns' undo_stage_hunk: the last hunk staged */

static int hunk_leader(Editor *e, int k)
{
	Of *o = tracked(e);
	if (!o) return -1;
	Repo *r = o->r;
	Foc f = { r, NULL, &r->st, 0 };
	if (k == 'S') { wgit("add", "--", o->rel, NULL); wsend(r, NULL, NULL); return 0; }
	if (k == 'p') { diff_open(e, r, T_UNSTAGED, o->rel); return 0; }
	if (k == 'u') {
		if (!staged_last.n) { core_msg(e, "No hunk to undo"); return -1; }
		Cmd *c = wgit("apply", "--reverse", "--cached", "-p0", "--unidiff-zero", "-", NULL);
		str_add(&c->in, staged_last.s, staged_last.n);
		staged_last.n = 0;
		wsend(r, NULL, NULL);
		return 0;
	}
	/* gitsigns' stage_hunk and reset_hunk: the block of signs under the cursor */
	int fs = file_in(&r->st, top(&r->st, T_UNSTAGED), o->rel);
	Blk bk;
	if (fs < 0 || (k != 's' && k != 'r') || !block_at(&r->st, fs, (long)e->win->line + 1, &bk)) {
		core_msg(e, "No hunk under the cursor");
		return -1;
	}
	if (!strncmp(r->st.pool.s + r->st.sec[fs].aux, "diff --cc", 9)) { core_msg(e, "No hunk under the cursor"); return -1; }
	Cmd *c = wgit("apply", k == 's' ? "--cached" : "--reverse", "-p0", "--unidiff-zero", "-", NULL);
	if (!c || block_patch(&r->st, fs, &bk, k == 's', &c->in)) { git_write_free(&W); return -1; }
	if (k == 's') { staged_last.n = 0; str_add(&staged_last, c->in.s, c->in.n); }
	wsend(r, &f, NULL);
	return 0;
}

/* SPC g B: the file on GitHub at the cursor's line, in the browser */
static int browse(Editor *e)
{
	Of *o = tracked(e);
	if (!o) return -1;
	char p[PATH_MAX], line[1024], url[2048] = "";
	snprintf(p, sizeof p, "%s/config", o->r->common);
	FILE *fp = fopen(p, "r");
	int in = 0;
	while (fp && fgets(line, sizeof line, fp)) {
		if (line[0] == '[') in = !strncmp(line, "[remote \"origin\"]", 17);
		char *u = strstr(line, "url = ");
		if (in && u) snprintf(url, sizeof url, "%s", u + 6);
	}
	if (fp) fclose(fp);
	url[strcspn(url, "\n")] = 0;
	char *h = strstr(url, "github.com");
	if (!h) { core_msg(e, "No GitHub remote"); return -1; }
	h += 11;
	size_t n = strlen(h);
	if (n > 4 && !strcmp(h + n - 4, ".git")) h[n - 4] = 0;
	char full[4096];
	snprintf(full, sizeof full, "https://github.com/%s/blob/%s/%s#L%zu", h,
	         o->r->br.head ? o->r->br.head : o->r->br.oid ? o->r->br.oid : "HEAD", o->rel, e->win->line + 1);
	if (proc_open(full)) { core_msg(e, "Cannot open %s", full); return -1; }
	return 0;
}

int git_leader(Editor *e, int prefix, int key)
{
	git_editor(e);
	if (prefix == 'h') return hunk_leader(e, key);
	char dir[PATH_MAX] = ".";
	if (e->buf && e->buf->path) {
		snprintf(dir, sizeof dir, "%s", e->buf->path);
		char *s = strrchr(dir, '/');
		if (s) *s = 0; else snprintf(dir, sizeof dir, ".");
	}
	Of *o = e->buf ? of(e->buf) : NULL;
	Repo *r = o && o->r ? o->r : git_repo(dir);
	switch (key) {
	case 'g': case 's': return git_status(e, dir);
	case 'B': return browse(e);
	}
	if (!r) { core_msg(e, "Not in a Git repository"); return -1; }
	Foc f = { r, NULL, &r->st, 0 };
	switch (key) {
	case 'l': log_open(e, r, "HEAD", NULL, NULL); return 0;
	case 'L':
		if (!o || !o->rel) { core_msg(e, "No file"); return -1; }
		log_open(e, r, "HEAD", NULL, o->rel);
		return 0;
	case 'd': diff_open(e, r, T_UNSTAGED, o && o->rel ? o->rel : NULL); return 0;
	case 'b': branch_checkout(e, &f); return 0;
	}
	core_msg(e, "No such git command");
	return -1;
}

/* gitsigns' signs from the unstaged diff (block_at) */
/* git_sign's cache: buffer (scnr), repository, build, file section */
static Repo *scr;
static unsigned long scgen;
static int scfs = -1;
int git_sign(const Buf *b, size_t l)
{
	if (b->nr != scnr || !scr || scr->gen != scgen) {
		Of *o = find(b);
		scnr = b->nr; scr = o ? o->r : NULL; scfs = -1;
		if (!scr || !o->rel) return 0;
		scgen = scr->gen;
		scfs = file_in(&scr->st, top(&scr->st, T_UNSTAGED), o->rel);
	}
	Blk bk;
	return scfs < 0 ? 0 : block_at(&scr->st, scfs, (long)l + 1, &bk);
}

/* lualine's branch component (branch/git_branch.lua): the ref name, else
   HEAD's first 6 characters; from the status's # branch lines */
const char *git_branch(const Buf *b)
{
	static char h[7];
	Of *o = find(b);
	const Branch *br = o && o->r ? &o->r->br : NULL;
	if (!br || !br->oid) return NULL;
	if (br->head) return br->head;
	snprintf(h, sizeof h, "%s", br->oid);
	return h;
}

/* git_free's part: no prompt, focus or remembered buffer survives */
void git_keys_free(void)
{
	memset(&P, 0, sizeof P);
	pend = 0;
	hr = NULL; ha = NULL;
	for (size_t i = 0; i < nofs; i++) free(ofs[i].rel);
	free(ofs); ofs = NULL; nofs = 0;
	str_free(&staged_last);
	scnr = 0; scr = NULL; scfs = -1;
	for (int i = 0; i < NK; i++) str_free(&D.files[i]);
	git_write_free(&W);
}

int git_status(Editor *e, const char *dir)
{
	git_editor(e);
	Repo *r = git_repo(dir);
	if (!r) { core_msg(e, "Not in a Git repository"); return -1; }
	if (git_headless) { hr = r; ha = NULL; }
	else if (git_buf(r->bufnr)) { if (!git_on(r->bufnr) && core_buf_goto(e, r->bufnr)) return -1; }
	else {
		if (core_edit_file(e, "", 0)) return -1;
		r->bufnr = e->buf->nr;
		if (r->st.nvis) git_fill(r->bufnr, &r->st, r->cur);
	}
	r->dirty = 1;                      /* magit-status refreshes the buffer */
	git_schedule(r);
	return 0;
}
