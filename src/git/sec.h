/* Package 17's inside: magit's sections (magit-section.el) over lines of
   text, and the parsers that turn git's output into them.  No processes and
   no editor here, so the tests and the fuzzer drive it directly. */
#ifndef GIT_SEC_H
#define GIT_SEC_H
#include <stddef.h>
#include "str.h"   /* (package 17's, agreed by thread 0) */

/* Section types, magit's names */
enum {
	T_STATUS, T_HEADERS, T_UNTRACKED, T_UNSTAGED, T_STAGED, T_STASHES,
	T_UNPUSHED, T_UNPULLED, T_RECENT, T_FILE, T_HUNK, T_COMMIT, T_STASH,
	T_INFO, T_LOG, T_DIFF, T_MERGING
};

typedef struct {
	int type, parent;          /* parent: index in View.sec, -1 for the root */
	int after;                 /* index after this section's subtree */
	size_t start, end;         /* its lines [start, end); start is the heading */
	size_t value;              /* offset of its NUL-terminated value in pool; 0 none */
	size_t aux;                /* file: its diff header; hunk: file status letter etc. */
	int hidden;
} Sec;

/* One line: [off, off + len) of pool; sec is the innermost section */
typedef struct { size_t off, len; int sec; } Line;

/* Remembered visibility (magit-section-visibility-cache): idents, each
   ending in "\x1e" plus '0' shown or '1' hidden. */
typedef struct { Str s; } Cache;

typedef struct {
	Str pool;                  /* starts with one NUL, so value 0 is "none" */
	Line *ln; size_t nln, cln;
	Sec *sec; int nsec; size_t csec;
	int open[8], nopen;        /* sections being built */
	Cache *cache;
	/* what is shown: visible lines and their text, rebuilt by sec_render */
	size_t *vis; size_t nvis, cvis;
	Str text;
} View;


void view_clear(View *v);      /* keeps the cache */
void view_free(View *v);
/* Builder, as magit-insert-section: open a section (its hidden default
   unless the cache says otherwise), add lines, close it. */
int  sec_open(View *v, int type, const char *value, size_t vlen, int hide);
int  sec_line(View *v, const char *s, size_t n);
void sec_close(View *v);
const char *sec_value(const View *v, int i);
/* magit-section-ident as a string: "type value" pairs up to the root */
int  sec_ident(const View *v, int i, Str *out);
int  sec_cache_set(View *v, int i);
/* Shown lines: text holds them joined by '\n' (each ends in '\n'), a hidden
   section's heading followed by "…" when it has a body. */
int  sec_render(View *v);
int  sec_at(const View *v, size_t line);  /* innermost section of a line */
int  sec_has_body(const View *v, int i);
/* The visible line showing line l, or the nearest visible one before it */
size_t sec_vis_of(const View *v, size_t l);

/* Parsers.  Each appends sections to v; the text is git's output. */
typedef struct {
	char *head;                /* branch name, NULL when detached */
	char *oid;                 /* HEAD's commit, "(initial)" before the first */
	char *upstream;            /* NULL without one */
	long ahead, behind;
	int ab;                    /* ahead and behind are known */
	Str untracked;             /* paths, each ending in NUL */
	int nuntracked;
	Str conflicts;             /* "XY path" of unmerged entries, each ending in NUL */
} Branch;
/* git status --porcelain=v2 -z --branch into b.  The other entries are not
   needed: the diffs say what changed. */
int  parse_status(const char *s, size_t n, Branch *b);
void branch_free(Branch *b);
/* magit-insert-untracked-files */
int  build_untracked(View *v, const Branch *b);
/* magit-insert-child-count: a heading ending in ':' gets " (N)" instead */
int  sec_child_count(View *v, int i);
/* git diff and git diff --cached (--no-prefix), washed as
   magit-diff-wash-diffs: a section per file (hidden by default in the
   status buffer, as magit's), a hunk section per hunk.  Returns how many
   files, -1 out of memory.  cached: the --cached diff, where magit leaves
   out unmerged paths. */
int  parse_diff(View *v, const char *s, size_t n, int file_hidden, int cached, const Branch *b);
/* git log --format=%m%x1f%h%x1f%D%x1f%s, one commit per line: commit
   sections whose mark is m (0 any); refs as magit-log-format-refs-labels
   shows them given the branch. */
int  parse_log(View *v, const char *s, size_t n, int m, const Branch *b);
/* the subject of the commit decorated with ref (e.g. "refs/remotes/origin/main"),
   or of the first commit when ref is NULL; into out */
int  log_subject(const char *s, size_t n, const char *ref, Str *out);
/* git log --graph --format=%h%x1f%D%x1f%aN%x1f%at%x1f%s: one commit
   section per line, "hash graph refs subject" in width - 30 columns, then
   magit's margin (author in 18 columns, the age at time now) */
int  parse_logbuf(View *v, const char *s, size_t n, const Branch *b, int width, long now);
/* git diff -p --stat (or show): the stat as one section headed by its
   summary, then the files, shown (magit-diff-mode) */
int  parse_diffbuf(View *v, const char *s, size_t n, const Branch *b);
int  parse_revbuf(View *v, const char *s, size_t n, const Branch *b);
int  parse_stashbuf(View *v, const char *s, size_t n, const Branch *b);
#endif
