/* The matcher of the pickers and the snippet popup (package 15,
   src/tools/match.c): snacks.picker's (core/matcher.lua and score.lua at
   my-nvim's pinned 882c996), which ports fzf's scoring.  Bytes, as Lua's
   string functions: lower case and character classes are ASCII only. */
#ifndef TOOLS_MATCH_H
#define TOOLS_MATCH_H
#include <stddef.h>

/* terms are split by spaces, so MATCH_MAX bytes hold at most half as many */
enum { MATCH_MAX = 256, MATCH_TERMS = MATCH_MAX / 2, MATCH_DEFAULT = 1000 };

typedef struct {
	const char *s;             /* the term, into Match.buf; lowered if icase */
	size_t n;
	int entropy;
	unsigned char icase, fuzzy, word, prefix, suffix, inverse, field;  /* field: 1 file, 2 unknown */
} Term;

typedef struct {
	char buf[MATCH_MAX];
	Term t[MATCH_TERMS];
	int nt;
	int grp[MATCH_TERMS + 1];  /* AND group g is t[grp[g] .. grp[g+1]) (ORed) */
	int ng;
	long line, col;            /* "file:line:col" typed: where to jump (line 0: none) */
	int filename_bonus;        /* snacks' matcher.filename_bonus (default on) */
} Match;

/* Parses the prompt (matcher.lua's M:init and M:_prepare).  Returns 0, or
   -1 if it is too long or has too many terms (then nothing matches). */
int match_init(Match *m, const char *q, size_t n);
/* The score of text (with file the item's file, or NULL), as M:match: 0
   if it does not match; the picker shows only scores above 0 (M:update). */
int match_score(const Match *m, const char *text, size_t n, const char *file, size_t fn);
/* The number the decimal digits at *p (before e) spell, saturating at
   LONG_MAX, with *p moved past them; -1 if there are none.  The prompt's
   file:line:col, rg's line:col and :make's errors read their numbers with it. */
long match_num(const char **p, const char *e);
#endif
