/* Piece table: the text model shared by the C prototype and its CBMC harness. */
#ifndef PT_H
#define PT_H
#include <stddef.h>
typedef struct { unsigned char buf; size_t start, len; } Piece;   /* buf 0 = original, 1 = add */
typedef struct {
  const char *orig; size_t orig_len;
  char *add; size_t add_len, add_cap;
  Piece *p; size_t n, cap;
  size_t len;                     /* invariant: len == sum of p[i].len */
} PT;
int  pt_init(PT *t, const char *orig, size_t n);
void pt_free(PT *t);
int  pt_insert(PT *t, size_t pos, const char *s, size_t n);   /* 0 ok, -1 bad pos / OOM */
int  pt_delete(PT *t, size_t pos, size_t n, char *out);       /* copies deleted bytes to out if non-NULL */
size_t pt_read(const PT *t, size_t pos, char *out, size_t n);
#endif
