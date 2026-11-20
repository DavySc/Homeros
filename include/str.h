/* A growable NUL-terminated string (str.c): git's and lsp's one copy.
   Thread 0's, agreed. */
#ifndef STR_H
#define STR_H
#include <stddef.h>

typedef struct { char *s; size_t n, cap; } Str;
/* 0, or -1 when out of memory (s unchanged) */
int  str_add(Str *s, const char *p, size_t n);
int  str_addz(Str *s, const char *p);
int  str_addf(Str *s, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void str_free(Str *s);
#endif
