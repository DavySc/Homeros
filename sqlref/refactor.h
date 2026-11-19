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
} Edits;

/* R2: every * and x.* starting in [a, z) becomes its column list,
   qualified by the source's exposed name.  0 ok, -1 out of memory, -2
   refused (why set) */
int refactor_star(const Res *r, size_t a, size_t z, Edits *out);
/* R3: every unqualified column in [a, z) gets its source's exposed name */
int refactor_qualify(const Res *r, size_t a, size_t z, Edits *out);
/* R7: what the name at byte pos is, as lines of text into out->buf */
int refactor_info(const Res *r, size_t pos, Edits *out);

/* the text with the edits applied (malloc'd, *n its length); NULL out of memory */
char *edits_apply(const Edits *ed, const char *text, size_t n, size_t *len);
/* the edited text resolves to the same thing everywhere: every column the
   same catalog column (or derived output), every statement the same output
   names and sources (the check every refactor passes before it is offered) */
int  refactor_same(const Snap *s, const char *a, size_t na, const char *b, size_t nb);
void edits_free(Edits *ed);
#endif
