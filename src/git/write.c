/* Writes, one at a time (Git.tla "ok"): a write runs only if the
   repository has not moved since the status it was chosen from was read
   (ActsOnSeen); its end marks the buffer dirty (Truthful).  git commit runs
   with with-editor's sleeping-editor protocol: git's editor names the
   message file and waits until the message buffer is finished or
   cancelled (with-editor-finish, with-editor-cancel). */
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "core.h"
#include "loop.h"
#include "gitint.h"


void git_write_free(Write *w)
{
	for (int i = 0; i < WSTEPS; i++) { str_free(&w->step[i].args); str_free(&w->step[i].in); }
	*w = (Write){ 0 };
}

static void say(const char *fmt, const char *a)
{
	Editor *e = git_ed();
	if (e) { core_msg(e, fmt, a); core_changed = 1; }
}

int git_queue(Repo *r, Write *w)
{
	if (r->qn == QMAX) { git_write_free(w); say("git: %s", "too many commands waiting"); return -1; }
	r->q[(r->qh + r->qn++) % QMAX] = *w;
	*w = (Write){ 0 };
	git_schedule(r);
	return 0;
}

static int run_step(Repo *r)
{
	r->editing = r->w.commit;
	return git_spawn(r, SLOT_WRITE, &r->w.step[r->wstep]);
}

/* the untracked file or directory to the trash (os.c, win.c) */
static int trash(Repo *r, const char *rel)
{
	char src[PATH_MAX];
	snprintf(src, sizeof src, "%s/%s", r->top, rel);
	size_t n = strlen(src);
	if (n && src[n - 1] == '/') src[n - 1] = 0;          /* an untracked directory */
	return git_trash(src);
}

static void finish(Repo *r)
{
	git_write_free(&r->w);
	r->busy = 0;
	r->editing = 0;
	r->ver++;                      /* Git.tla Finish */
	r->dirty = 1;
	git_schedule(r);
	/* a buried log or diff is read again when opened again (aux_open) */
	for (Aux *a = r->aux; a; a = a->next) if (git_aux_shown(a)) git_aux_fill(a);
}

void git_start_write(Repo *r)
{
	r->w = r->q[r->qh];
	r->q[r->qh] = (Write){ 0 };
	r->qh = (r->qh + 1) % QMAX;
	r->qn--;
	git_drain(r);
	if (r->w.ver != r->ver) {
		/* Git.tla: dropped, the user saw another repository */
		say("git: %s", "the repository changed since it was shown; nothing done");
		git_write_free(&r->w);
		git_schedule(r);
		return;
	}
	r->busy = BUSY_WRITE;
	r->err.n = 0;
	r->wstep = 0;
	if (r->w.trash) {
		/* the paths are step[n]'s arguments */
		Cmd *t = &r->w.step[r->w.n];
		const char *p = t->args.s;
		for (int i = 0; i < t->argc; i++, p += strlen(p) + 1)
			if (trash(r, p)) { say("git: cannot move %s to the trash", p); break; }
	}
	if (!r->w.n || run_step(r)) finish(r);
}

void git_write_step_done(Repo *r)
{
	int rc = r->rc[SLOT_WRITE];
	if (rc && !r->w.step[r->wstep].okfail) {
		/* the last line git wrote to stderr says why */
		Str *e = &r->err;
		while (e->n && e->s[e->n - 1] == '\n') e->n--;
		size_t a = e->n;
		while (a && e->s[a - 1] != '\n') a--;
		if (r->w.cancel) say("%s", "Commit canceled.");
		else if (e->n) { e->s[e->n] = 0; say("%s", e->s + a); }
		else say("%s", "git failed");
		finish(r);
		return;
	}
	if (++r->wstep < r->w.n && !run_step(r)) return;
	if (r->w.ok[0]) say("%s", r->w.ok);
	finish(r);
}

/* git's editor named the message file: open it (with-editor) */
void git_editor_seen(Repo *r)
{
	Str *o = &r->out[SLOT_WRITE];
	char *p = o->s ? strstr(o->s, "HOMEROS-EDIT ") : NULL;
	if (!p) return;
	char *e = strchr(p, '\n');
	if (!e) return;
	*e = 0;
	char path[PATH_MAX];
	if (git_abs(p + 13)) snprintf(path, sizeof path, "%s", p + 13);
	else snprintf(path, sizeof path, "%s/%s", r->top, p + 13);
	free(r->editmsg);
	r->editmsg = strdup(path);
	r->editing = 2;
	o->n = 0;
	Editor *ed = git_ed();
	r->msgnr = 0;
	if (git_headless || (ed && !core_edit_file(ed, path, 1))) {
		if (!git_headless) r->msgnr = ed->buf->nr;
		say("%s", "Type Z Z to finish, Z Q to cancel");
	} else {
		/* no buffer to edit in: cancel, as with-editor when the buffer dies */
		git_editor_done(r, 0);
	}
	core_changed = 1;
}

/* with-editor-finish (ok) and with-editor-cancel */
void git_editor_done(Repo *r, int ok)
{
	Editor *ed = git_ed();
	if (r->editing != 2) return;
	Buf *b = git_buf(r->msgnr);
	if (b) {
		if (ok && core_modified(b) && core_write(ed, b, 1)) ok = 0;
		if (git_buf(r->bufnr) && ed->buf == b) core_buf_goto(ed, r->bufnr);
		core_buf_delete(ed, b->nr, 1);
	}
	r->msgnr = 0;
	r->editing = 3;
	r->w.cancel = !ok;
	proc_write(r->pid[SLOT_WRITE], ok ? "y\n" : "n\n", 2);
	proc_close(r->pid[SLOT_WRITE]);
}
