/* Org (package 16, src/org/): the parser.  Org's own parser, org-element
   (Org 9.8.7 in Emacs 31.1), is the reference: org_parse gives the elements
   that org-element-parse-buffer gives at granularity element, with the same
   positions (bytes from 0 here, characters from 1 there).  Objects inside
   paragraphs (markup, links, timestamps) are not parsed yet.  Build B won
   the A/B comparison (2026-11-17). */
#ifndef ORG_H
#define ORG_H
#include <stddef.h>

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
#endif
