/* Lean 4's colours (lean.c), on tsql.c's index of token starts as its
   language 3. */
#ifndef LEAN_H
#define LEAN_H
#include <stddef.h>
#include "tsql.h"

/* the token at p, a region whole; its end.  With f, faces for its bytes
   in [a, z) into f, from f[0]. */
size_t lean_lex(TsqlRd *r, size_t p, size_t a, size_t z, unsigned char *f);
#endif
