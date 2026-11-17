/* Package 17's shared state: one Repo per work tree that has been shown. */
#ifndef GIT_INT_H
#define GIT_INT_H
#include "core.h"
#include "sec.h"

enum { SLOT_STATUS, SLOT_LOG, SLOT_DIFF, SLOT_CACHED, SLOT_DESCRIBE, SLOT_WRITE, NSLOT };
enum { BUSY_REFRESH = 1, BUSY_WRITE };
enum { LOG_NONE, LOG_RECENT, LOG_LR, LOG_BEHIND };
enum { CMD_MAX = 64, QMAX = 8, WSTEPS = 8 };

/* One git command line: arguments as NUL-ended strings, stdin text */
typedef struct {
	Str args, in;
	int argc;
	int keepin;                /* stdin stays open (the commit's editor reads it) */
	int okfail;                /* a failure does not stop the steps after it */
} Cmd;

/* A write: steps run one after another, each only if the one before
   succeeded; ver is the version of the status it was chosen from. */
typedef struct {
	Cmd step[WSTEPS];
	int n;
	unsigned long ver;
	int commit;                /* runs git's editor (git commit) */
	int trash;                 /* files to move to the trash first: in step[n] */
	int cancel;                /* the commit message was cancelled */
	char ok[96];               /* said when every step succeeded ("Git finished" for magit's async commands) */
} Write;

struct Repo;
typedef struct { struct Repo *r; int slot; } Run;

/* A log or diff buffer (magit-log-mode, magit-diff-mode) */
typedef struct Aux {
	View v;
	Buf *buf;
	size_t cur;                /* the cursor while no window shows it */
	int type;                  /* T_LOG or T_DIFF */
	int dtype;                 /* a diff's type: T_UNSTAGED, T_STAGED, else committed */
	int split;                 /* opened in a new window (q closes it) */
	Cmd cmd;                   /* how to fill it again (g r) */
	Str out;
	int pid, busy;
	unsigned long ver;          /* Repo.ver when it was read: a write from it acts on that */
	struct Repo *r;
	Run run;
	struct Aux *next;
} Aux;

typedef struct Repo {
	char *top, *gitdir;
	unsigned long gen;         /* builds of st, for git_sign's cache */
	View st, old;              /* the status buffer's sections; old: the previous ones, for the cursor */
	Cache cache;
	Branch br;
	Buf *buf;                  /* the status buffer, or NULL */
	size_t cur;                /* its cursor (visible line) when no window shows it */
	/* Git.tla: busy is running > 0; ver is repo, shown is shown */
	int busy, phase, left, nproc, logmode;
	char merging[48];          /* MERGE_HEAD's commit while a merge runs, else "" */
	unsigned long ver, shown, pend;
	int dirty;
	Str out[NSLOT], err;
	int rc[NSLOT], pid[NSLOT];
	Run run[NSLOT];
	Write q[QMAX], w;          /* w: the write running */
	int qh, qn, wstep;
	/* inotify */
	int ifd, watched;
	char **wdpath; size_t nwd;
	Str ignp; size_t *ign, nign;
	/* the commit message buffer (with-editor) */
	int editing;               /* 1 waiting for git's editor line, 2 the buffer is open */
	char *editmsg;
	Buf *msgbuf;
	Aux *aux;
	struct Repo *next;
} Repo;

extern Repo *git_repos;
/* Tests: views live without core buffers (ref/core.c has one buffer); the
   cursor is Repo.cur or Aux.cur and the commit message is written by the test */
extern int git_headless;
extern Aux *git_ha;                /* headless: the log or diff view keys go to, else the status */
enum { P_YN = 1, P_TEXT, P_POP };
int   git_prompt(void);           /* the prompt being answered: P_*, 0 none */

Repo *git_repo(const char *dir);
void  git_schedule(Repo *r);
void  git_drain(Repo *r);
void  git_editor(Editor *e);
Editor *git_ed(void);
void  cmd_add(Cmd *c, const char *a);
void  cmd_git(Cmd *c, int read);
void  cmd_editor(Cmd *c);
void  cmd_free(Cmd *c);
int   git_spawn(Repo *r, int slot, Cmd *c);
void  git_editor_done(Repo *r, int ok);
/* keys.c */
int   git_status(Editor *e, const char *dir);
void  git_aux_free(Aux *a);
void  git_aux_fill(Aux *a);
void  git_keys_free(void);
int   git_visit_target(const View *v, size_t vis, int dtype, char *rel, size_t nrel, size_t *line);
/* status.c */
int   git_build_status(Repo *r);
void  git_show(Repo *r, View *v);
int   git_buf_alive(const Buf *b);
void  git_fill(Buf *b, View *v, size_t cur);
/* write.c */
void  git_start_write(Repo *r);
void  git_write_step_done(Repo *r);
void  git_write_free(Write *w);
int   git_queue(Repo *r, Write *w);
void  git_editor_seen(Repo *r);
#endif
