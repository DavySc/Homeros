/* Running a T-SQL script: its batches as sqlcmd splits them at GO lines. */
#ifndef SQL_RUN_H
#define SQL_RUN_H
#include <stddef.h>
#include "text.h"
#include "str.h"

/* A batch: [a, z) of the text, run count times (GO n; 1 without n). */
typedef struct { size_t a, z; long count; size_t pre, npre; } SqlBatch;   /* pre: its DECLAREs, sql.c's */
typedef struct { SqlBatch *b; size_t n, cap; } SqlBatches;

/* The batches of t's [a, z), as sqlcmd 18 splits a script (KB chapter 13,
   measured).  GO is a separator on a line of its own, after blanks or block
   comments, case ignored, with an optional count and line comment; outside
   strings and comments (the T-SQL lexer's tokens; block comments nest,
   as in SQL Server, where sqlcmd ends one at its first star-slash).  Batches of
   empty lines only are left out.  0 ok, -1 out of memory, or the 1-based line of
   a GO line sqlcmd refuses ("Syntax error at line N near command 'GO'"):
   out then holds the batches before it, which sqlcmd runs before it stops
   (measured), and the text between them and that line is dropped. */
long sql_split(const Text *t, size_t a, size_t z, SqlBatches *out);
/* The @variables t's [a, z), one batch, uses without declaring them, each
   once, as "@name\0" into out: what F5 asks for (Davy, 2026-11-27).  Not
   @@x, nor in strings, comments or [names]; not one its DECLARE names
   (case ignored, as a case-insensitive server); not an EXEC's parameter
   name (@p = after the procedure); none in CREATE or ALTER PROCEDURE,
   FUNCTION or TRIGGER (its parameters are declared).  With decls, each
   DECLARE's names and what follows them ("@a\0int = 1\0") go there too:
   the prompt's default.  0 ok, -1 out of memory. */
int sql_vars(const Text *t, size_t a, size_t z, Str *out, Str *decls);
#endif
