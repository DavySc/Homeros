/* T-SQL formatting (package 18, src/sqlfmt/): SQLDiff's `sqldiff fmt` with
   Davy's settings (keywords lower case), only the whitespace between tokens
   and the case of reserved keywords change.
   The whole text t is formatted; the result is the edit for the tokens
   that start in bytes [*a, *z) (pass 0 and text_len(t) for all of it): on
   return, replace bytes [*a, *z) of t by the *n bytes at *res, which stay
   valid until the next call.  An edit with *a == *z and *n == 0 changes
   nothing.  Returns how many top-level statements were kept as written
   because the output check failed for them (when it cannot be pinned to
   one, the whole text is kept and the count is every statement's), or -1
   out of memory (t unchanged, no edit). */
#ifndef SQLFMT_H
#define SQLFMT_H
#include <stddef.h>
#include "text.h"
int sqlfmt(const Text *t, size_t *a, size_t *z, const char **res, size_t *n);
#endif
