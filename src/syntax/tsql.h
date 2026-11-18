/* T-SQL by word lists, as SSMS colours it (tsql.c).  The lexer is shared
   with SQLDiff's formatter, so its tokens are SQL Server's own. */
#ifndef TSQL_H
#define TSQL_H
#include <stddef.h>
#include "text.h"

enum {
	TQ_SPACE, TQ_WORD, TQ_IDENT,   /* IDENT: [bracketed] or "quoted" */
	TQ_VAR,                        /* @x, @@x, #t, ##t */
	TQ_STRING,                     /* '..' or N'..', '' inside */
	TQ_NUMBER, TQ_OP,              /* OP: + - * / % = < > ! & | ^ ~ :, and
	                                  SQL Server's two-byte ones as one */
	TQ_PUNCT,                      /* , ( ) ; . */
	TQ_COMMENT,                    /* -- to the line's end (\r or \n); nested slash-star */
};
/* tsql_word's classes, any of them */
enum { TQ_KEYWORD = 1, TQ_FUNCTION = 2, TQ_OPERATOR = 4, TQ_RECASE = 8 };

typedef struct Tsql Tsql;
Tsql  *tsql_new(const Text *t);
void   tsql_free(Tsql *q);
/* before every change, while t is still the old text: at pos, ndel bytes
   become nins */
void   tsql_edit(Tsql *q, const Text *t, size_t pos, size_t ndel, size_t nins);
/* deferred: fills the index of token starts to the text's end, asking stop
   now and then; syntax_update's 0, 1 (a window was left plain: draw again)
   or 2 (stopped) */
int    tsql_update(Tsql *q, int (*stop)(void));
/* one face per byte of [a, z) into face, which the caller cleared; no
   allocation */
void   tsql_faces(Tsql *q, size_t a, size_t z, unsigned char *face);
/* The text's bytes through a window: set t, and n to 0 (again after the
   text changes). */
typedef struct {
	const Text *t;
	size_t a, n;
	char b[4096];
	int open;      /* tsql_token's last token ran to the end unclosed: a
	                  string, a [name] or "name", or a slash-star comment */
} TsqlRd;
/* byte i, or -1 past the end */
int    tsql_byte(TsqlRd *r, size_t i);
/* the token starting at pos (its kind into *kind); its end, pos at the end */
size_t tsql_token(TsqlRd *r, size_t pos, int *kind);
/* the classes of the n bytes at w, any case; 0 for a word it lacks.  With
   id, the word's number into *id, from 1 to tsql_nwords, 0 when it lacks
   it (the formatter maps these to its own words). */
int    tsql_word(const char *w, size_t n, size_t *id);
extern const size_t tsql_nwords;
#endif
