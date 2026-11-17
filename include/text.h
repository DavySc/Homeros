/* Text: the bytes of one buffer, in a rope (package 1, src/text/); this
   header is its whole contract.
   Offsets are bytes.  Lines end with '\n'; line l (from 0) starts after the
   l-th '\n', so a text holds text_lines() lines plus the bytes after the last
   '\n' (the core keeps that tail empty, see core.h). */
#ifndef TEXT_H
#define TEXT_H
#include <stddef.h>

typedef struct Text Text;

Text  *text_new(const char *s, size_t n);   /* NULL when out of memory */
void   text_free(Text *t);
size_t text_len(const Text *t);
/* 0 ok, -1 out of memory or pos > len (text unchanged) */
int    text_insert(Text *t, size_t pos, const char *s, size_t n);
/* 0 ok, -1 range past the end (text unchanged) */
int    text_delete(Text *t, size_t pos, size_t n);
/* Copies up to n bytes from pos into out; returns how many were copied. */
size_t text_read(const Text *t, size_t pos, char *out, size_t n);
size_t text_lines(const Text *t);                 /* number of '\n' */
size_t text_line_start(const Text *t, size_t l);  /* l <= text_lines(); else text_len */
size_t text_line_of(const Text *t, size_t pos);   /* number of '\n' before pos */
#endif
