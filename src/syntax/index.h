/* tsql.c's index of token starts for a lexer that reads a line at a time
   and carries a state from line to line (Lean's, lean.c): the index keeps
   each kept start's state, so a window is read from the kept line before
   it, not from where a long comment began. */
#ifndef INDEX_H
#define INDEX_H
#include <stdint.h>
#include "tsql.h"

/* a state at a line's start, what the lexer puts there; all zero at the
   text's start */
typedef struct { uint32_t w[4]; } TsqlSt;
/* the line from p, a line's start, in state *s: its end past its '\n' (the
   text's end on the last line), and the state there into *s.  With f,
   faces for its bytes in [a, z) into f, from f[0]. */
typedef size_t TsqlLine(TsqlRd *r, size_t p, TsqlSt *s, size_t a, size_t z, unsigned char *f);
/* q, from tsql_new, reads by lines through line */
void   tsql_lines(Tsql *q, TsqlLine *line);
/* the '\n' that ends p's line, or the text's end */
size_t tsql_line_end(TsqlRd *r, size_t p);
#endif
