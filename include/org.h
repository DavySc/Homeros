/* Org (package 16, src/org/): the parser.  Org's own parser, org-element
   (Org 9.8.7 in Emacs 31.1), is the reference: org_parse gives the elements
   that org-element-parse-buffer gives at granularity element, with the same
   positions (bytes from 0 here, characters from 1 there).  Objects inside
   paragraphs (markup, links, timestamps) are not parsed yet.  Build B won
   the A/B comparison (2026-11-17). */
#ifndef ORG_H
#define ORG_H
#include <stddef.h>
#include "text.h"

/* org-element-all-elements without inlinetask (org-inlinetask is not loaded
   by default, so 15 stars make a headline) */
enum {
	ORG_HEADLINE, ORG_SECTION, ORG_PLANNING, ORG_PROPERTY_DRAWER, ORG_NODE_PROPERTY,
	ORG_DRAWER, ORG_CLOCK, ORG_COMMENT, ORG_KEYWORD, ORG_BABEL_CALL, ORG_PARAGRAPH,
	ORG_PLAIN_LIST, ORG_ITEM, ORG_TABLE, ORG_TABLE_ROW, ORG_SRC_BLOCK,
	ORG_EXAMPLE_BLOCK, ORG_EXPORT_BLOCK, ORG_COMMENT_BLOCK, ORG_VERSE_BLOCK,
	ORG_QUOTE_BLOCK, ORG_CENTER_BLOCK, ORG_SPECIAL_BLOCK, ORG_DYNAMIC_BLOCK,
	ORG_LATEX_ENVIRONMENT, ORG_FIXED_WIDTH, ORG_HORIZONTAL_RULE,
	ORG_FOOTNOTE_DEFINITION, ORG_DIARY_SEXP, ORG_TYPES
};
#define ORG_NONE ((size_t)-1)

/* org-element's :begin, :post-affiliated, :contents-begin, :contents-end
   and :end; cbeg and cend are ORG_NONE when it has none. */
typedef struct {
	size_t begin, aff, cbeg, cend, end;
	size_t parent;   /* index of the enclosing node, ORG_NONE at the top */
	int type;
} OrgNode;

/* The elements of s[0, n) in document order (a node before its children),
   *count of them, in an array to free(); NULL when out of memory.
   A subtree (a headline's begin to its end) parses alone to the same nodes,
   shifted, unless one of them ends past it (only a babel call can: its
   brackets are matched to the end of the text, as Org does). */
OrgNode *org_parse(const char *s, size_t n, size_t *count);
/* After an edit replaced del bytes at pos by ins bytes, making s[0, n):
   updates *e and *count (from org_parse or org_edit, for the text before)
   to what org_parse(s, n) gives, reparsing only the top-level subtrees the
   edit touched.  0 ok; -1 out of memory, *e then freed and NULL. */
int org_edit(OrgNode **e, size_t *count, const char *s, size_t n, size_t pos, size_t del, size_t ins);

/* The keywords of org-todo-keywords, done ones from org_done on; NULL ends. */
extern const char *const org_todo[];
enum { org_done = 3 };

/* A headline's title fields, as org-element--headline-parse-title reads the
   line at begin: level (stars), todo (index in org_todo or -1), pri (Org's
   priority value or -1), commented (COMMENT), the title [ta, tz) trimmed
   (:raw-value) and the tags [ga, gz) between their outer colons (ga == gz
   when there are none). */
typedef struct { int level, todo, pri, commented; size_t ta, tz, ga, gz; } OrgHead;
void org_head(const char *s, size_t n, size_t begin, OrgHead *h);

/* A timestamp as org-element-timestamp-parser reads it: [begin, end) with
   the blanks after it; type 'a' active, 'A' active-range, 'i' inactive,
   'I' inactive-range, 'd' diary; range 0, 'd' daterange, 't' timerange;
   the start and end fields -1 when Org has nil; rep 0, '+' cumulate, '#'
   catch-up (++), '.' restart (.+); warn 0, 'a' all (-), 'f' first (--);
   units h d w m y. */
typedef struct {
	size_t begin, end;
	char type, range, rep, rep_unit, rep_dl_unit, warn, warn_unit;
	int y, m, d, h, min, y2, m2, d2, h2, min2, rep_n, rep_dl_n, warn_n;
} OrgTs;
/* The timestamp at p, read as in a paragraph or clock line
   (case-fold-search on); 0 when there is none. */
int org_ts(const char *s, size_t n, size_t p, OrgTs *t);
/* A planning node's CLOSED, DEADLINE and SCHEDULED in t[0], t[1], t[2], as
   org-element-planning-parser reads them; returns which (bits 1, 2, 4). */
int org_planning(const char *s, size_t n, const OrgNode *e, OrgTs t[3]);
/* A clock node's :value in *t (returns 0 when it has none) and its
   :duration [dur[0], dur[1]), empty while the clock runs. */
int org_clock(const char *s, size_t n, const OrgNode *e, OrgTs *t, size_t dur[2]);

/* Org's colours for the syntax module (package 12), shaped as tsql.h: the
   faces (grid.h) that Org's own font-lock gives a whole buffer
   (org-set-font-lock-defaults); the objects inside paragraphs (links,
   emphasis, footnotes) are not coloured yet. */
typedef struct OrgFx OrgFx;
OrgFx *orgfx_new(const Text *t);
void   orgfx_free(OrgFx *f);
/* Called before every change, while t is still the old text: at pos, ndel
   bytes become nins. */
void   orgfx_edit(OrgFx *f, const Text *t, size_t pos, size_t ndel, size_t nins);
/* Deferred work, after a frame: reads the changed bytes and reparses the
   subtrees they touch (org_edit, one step: stop is not asked).  0 nothing
   changed, 1 colours changed. */
int    orgfx_update(OrgFx *f, int (*stop)(void));
/* One face per byte of [a, z) into face, cleared by the caller; no
   allocation.  Before an update, the bytes an edit did not touch keep the
   colours they had. */
void   orgfx_faces(OrgFx *f, size_t a, size_t z, unsigned char *face);
/* the fold level of line, as nvim-orgmode's foldexpr: the stars of the
   headline whose subtree holds it (org-current-level), 0 before the first;
   *start is 1 on the headline itself.  It sees edits made since the last
   update (it updates first). */
int    orgfx_foldexpr(OrgFx *f, size_t line, int *start);
#endif
