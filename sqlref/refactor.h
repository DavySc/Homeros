/* Refactors (KB 04-refactors.md): each is a function of the resolved text
   (resolve.h) giving edits, or refusing with a reason.  None writes to
   the server. */
#ifndef REFACTOR_H
#define REFACTOR_H
#include "resolve.h"

typedef struct { uint32_t a, z; uint32_t s, n; } Edit;   /* replace bytes [a, z) by buf[s .. s+n) */
typedef struct {
	Edit *e; size_t ne, ecap;
	char *buf; size_t nbuf, bcap;
	const char *why;     /* set when a refactor refuses */
	char msg[512];       /* why, when it is made up */
} Edits;

/* R2: every * and x.* starting in [a, z) becomes its column list,
   qualified by the source's exposed name.  0 ok, -1 out of memory, -2
   refused (why set) */
int refactor_star(const Res *r, size_t a, size_t z, Edits *out);
/* R3: every unqualified column in [a, z) gets its source's exposed name */
int refactor_qualify(const Res *r, size_t a, size_t z, Edits *out);
/* R7: what the name at byte pos is, as lines of text into out->buf */
int refactor_info(const Res *r, size_t pos, Edits *out);

/* R1 across code: every reference to snapshot column col, straight to
   its table (not through a derived table or CTE, whose own column keeps
   its name), becomes to; a lone select-list column keeps the output name
   it gave (to AS old).  Refuses where the result would differ: a * that
   includes the column, a name that may be the column but is not known */
int refactor_rename_col(const Res *r, long col, Name to, Edits *out);

/* the script renaming column col to `to` (into ed->buf): sp_rename, then
   an ALTER of every module of the snapshot whose text changes, in one
   transaction; every module passes the meaning check against after (s
   with the rename).  Refuses as the server would (a blocker the snapshot
   names) or as refactor_rename_col does for some module (the reason
   names it). */
int refactor_rename_script(const Snap *s, const Snap *after, long col, Name to, Edits *ed);

/* R1 inside one statement: the alias or CTE name at byte pos, and every
   reference to it, become to.  Refuses elsewhere (a temp table or table
   variable outlives the statement; a table's own name is no alias); a
   capture shows in the meaning check the caller runs. */
int refactor_rename_at(const Res *r, size_t pos, Name to, Edits *ed);

/* R9: the CTE named at byte pos becomes a derived table at each use, and
   its definition goes; refuses a recursive one.  The reverse: the derived
   table named at pos becomes a CTE of its statement (before the CTE whose
   body holds it, else after the last one, else a new WITH). */
int refactor_inline_cte(const Res *r, size_t pos, Edits *ed);
int refactor_extract_cte(const Res *r, size_t pos, Edits *ed);

/* the text with the edits applied (malloc'd, *n its length); NULL out of memory */
char *edits_apply(const Edits *ed, const char *text, size_t n, size_t *len);
/* text b under snapshot t resolves to what text a means under s (the
   same snapshot, or s after a rename): every column the same catalog
   column (or derived output), every statement the same output names and
   sources (the check every refactor passes before it is offered) */
int  refactor_same(const Snap *s, const char *a, size_t na, const Snap *t, const char *b, size_t nb);
void edits_free(Edits *ed);
#endif
