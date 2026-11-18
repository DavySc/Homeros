/* T-SQL formatting (src/sqlfmt/), SQLDiff's `sqldiff fmt` with Davy's
   settings.  Formats the whole text and returns the edit for the tokens
   starting in bytes [*a, *z) (the whole text: 0 and text_len): replace
   [*a, *z) by the n bytes at *res, valid until the next call.  Returns how
   many top-level statements were kept as written (their check failed), or
   -1 out of memory. */
#ifndef SQLFMT_H
#define SQLFMT_H
#include <stddef.h>
#include "text.h"
int sqlfmt(const Text *t, size_t *a, size_t *z, const char **res, size_t *n);
#endif
