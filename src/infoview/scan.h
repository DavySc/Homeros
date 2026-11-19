/* JSON text, looked into in place (scan.c).  A value is named by the
   offset of its first byte in s[0, n); NONE is no value. */
#ifndef INFOVIEW_SCAN_H
#define INFOVIEW_SCAN_H
#include <stddef.h>
#include <string.h>
#include "str.h"

#define NONE ((size_t)-1)

char  *j_copy(const char *s, size_t n);                 /* s[0, n) 0-ended, malloc'd */
size_t j_ws(const char *s, size_t n, size_t i);         /* i past JSON whitespace */
size_t j_skip(const char *s, size_t n, size_t i);       /* the end of the value at i, or NONE */
size_t j_next(const char *s, size_t n, size_t v);       /* past the comma after it, or NONE */
size_t j_len(const char *s, size_t n, size_t v);        /* its length, 0 for none */
int    j_is(const char *s, size_t n, size_t v, const char *lit);   /* the string lit, unescaped? */
/* the value of key in the object at o, or element k of the array at a: a
   value j_skip ends, or NONE */
size_t j_member(const char *s, size_t n, size_t o, const char *key);
size_t j_elem(const char *s, size_t n, size_t a, int k);
long   j_hex4(const char *s, size_t n, size_t i);       /* 4 hex digits at i, or -1 */
/* The string at v decoded to UTF-8 (lone surrogates U+FFFD), malloc'd,
   its length in *out; NULL if v is no string. */
char  *j_str(const char *s, size_t n, size_t v, size_t *out);
void   j_quote(Str *d, const char *s, size_t n);        /* s as the inside of a JSON string */
#endif
