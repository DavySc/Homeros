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
	TQ_COMMENT,                    /* -- to the line's end; nested slash-star */
};
/* tsql_word's classes, any of them */
enum { TQ_KEYWORD = 1, TQ_FUNCTION = 2, TQ_OPERATOR = 4, TQ_RECASE = 8 };

typedef struct Tsql Tsql;
Tsql  *tsql_new(const Text *t);
void   tsql_free(Tsql *q);
/* before every change, at pos, while t is still the old text */
void   tsql_edit(Tsql *q, const Text *t, size_t pos);
/* deferred: grows the index of token starts to the text's length */
void   tsql_update(Tsql *q);
/* one face per byte of [a, z) into face, no allocation */
void   tsql_faces(Tsql *q, size_t a, size_t z, unsigned char *face);
/* the token starting at pos (its kind into *kind); its end, pos at the end */
size_t tsql_token(const Text *t, size_t pos, int *kind);
/* the classes of the n bytes at w, any case; 0 for a word it lacks */
int    tsql_word(const char *w, size_t n);
#endif
