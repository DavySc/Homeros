/* Running a T-SQL script: its batches as sqlcmd splits them at GO lines. */
#ifndef SQL_RUN_H
#define SQL_RUN_H
#include <stddef.h>
#include "text.h"

/* A batch: [a, z) of the text, run count times (GO n; 1 without n). */
typedef struct { size_t a, z; long count; } SqlBatch;
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
#endif
