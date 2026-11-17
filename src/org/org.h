/* Org (package 16, src/org/): what Org's own parser, org-element (Org 9.8.7
   in Emacs 31.1), makes of a buffer at element granularity, as a flat list
   of nodes in document order, kept current by reparsing only the headline
   chunks an edit touches.  Offsets are bytes (org-element's positions are
   characters; src/org/oracle/oracle.el converts).  TODO keywords are
   Davy's: TODO NEXT WAIT | DONE CANCELLED.  Objects (links, emphasis,
   timestamps in text) are not in the list: org_timestamp reads one where
   asked. */
#ifndef ORG_H
#define ORG_H
#include <stddef.h>
#include "text.h"

/* org-element-all-elements but inlinetask (org-inlinetask is not loaded) */
enum {
	ORG_HEADLINE = 1, ORG_SECTION, ORG_PLANNING, ORG_PROPERTY_DRAWER, ORG_NODE_PROPERTY,
	ORG_DRAWER, ORG_CLOCK, ORG_PARAGRAPH, ORG_PLAIN_LIST, ORG_ITEM, ORG_TABLE,
	ORG_TABLE_ROW, ORG_KEYWORD, ORG_BABEL_CALL, ORG_SRC_BLOCK, ORG_EXAMPLE_BLOCK,
	ORG_EXPORT_BLOCK, ORG_COMMENT_BLOCK, ORG_QUOTE_BLOCK, ORG_CENTER_BLOCK,
	ORG_VERSE_BLOCK, ORG_SPECIAL_BLOCK, ORG_DYNAMIC_BLOCK, ORG_COMMENT,
	ORG_FIXED_WIDTH, ORG_HORIZONTAL_RULE, ORG_LATEX_ENVIRONMENT, ORG_DIARY_SEXP,
	ORG_FOOTNOTE_DEFINITION
};

typedef struct {
	size_t begin, end;  /* :begin (affiliated keywords included), :end (blank lines after included) */
	size_t post;        /* :post-affiliated */
	size_t cbeg, cend;  /* :contents-begin, :contents-end; cend == 0 when there are none */
	unsigned char type;
	unsigned depth;  /* 0 for a top-level headline or the first section; a child is one deeper */
	unsigned level;  /* a headline's stars (:true-level); else the depth below the
	                    headline whose section holds it, the section being 1 */
} OrgNode;

typedef struct Org Org;

Org  *org_new(const Text *t);  /* NULL when out of memory */
void  org_free(Org *o);
/* After t changed at pos, ndel bytes replaced by nins: reparses the
   headline chunks the change touched.  0 ok, -1 out of memory (o is then
   empty until the next successful org_edit). */
int   org_edit(Org *o, const Text *t, size_t pos, size_t ndel, size_t nins);
size_t org_count(const Org *o);
const OrgNode *org_nodes(const Org *o);

/* A headline's own line, as org-element--headline-parse-title reads it
   (its :true-level is the node's level): offsets into t, an empty range
   when absent. */
typedef struct {
	size_t todo, todo_end; /* :todo-keyword */
	int done;              /* :todo-type done */
	int priority;          /* :priority: the letter, or the number for [#1]; -1 when none */
	int commented;         /* :commentedp */
	size_t title, title_end; /* :raw-value before trimming */
	size_t tags, tags_end; /* the ":a:b:" group; split it as org-split-string does */
} OrgHeadline;
void org_headline(const Text *t, const OrgNode *h, OrgHeadline *out);

/* org-element-timestamp-parser at pos, case folded as in a clock line or a
   paragraph: 0 when there is no timestamp. */
typedef struct {
	size_t begin, end;     /* end includes the blanks after it, as Org's */
	char type;             /* a active, A active-range, i inactive, I inactive-range, d diary */
	char range;            /* 0, 'd' daterange, 't' timerange */
	int y, m, d, h, min;   /* start; -1 when absent (a diary timestamp has no date) */
	int y2, m2, d2, h2, min2;
	char rep;              /* 0, '+' cumulate, '#' catch-up (++), '.' restart (.+) */
	int rep_n; char rep_unit;  /* h d w m y */
	int rep_dl_n; char rep_dl_unit;  /* the /3d of a habit, 0 when none */
	char warn;             /* 0, 'a' all (-), 'f' first (--) */
	int warn_n; char warn_unit;
} OrgTimestamp;
int org_timestamp(const Text *t, size_t pos, OrgTimestamp *out);
/* A planning line's CLOSED, DEADLINE and SCHEDULED, as org-element-planning-parser
   reads them; returns which were found (bits 1, 2, 4). */
int org_planning(const Text *t, const OrgNode *p, OrgTimestamp out[3]);
/* A clock line's :value (0 when it has none) and its duration's range
   [dur[0], dur[1]), empty while it runs (org-element-clock-parser). */
int org_clock(const Text *t, const OrgNode *c, OrgTimestamp *out, size_t dur[2]);
#endif
