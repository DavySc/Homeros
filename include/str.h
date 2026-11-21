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
/* A child's output comes in pieces cut anywhere (loop.h): appends b[0, n)
   to part and calls fn with each line it completes, without its '\n' and
   NUL-terminated in part's memory; what follows the last '\n' stays in part
   for the next call.  0, or -1 when out of memory (the rest of b dropped). */
int  str_lines(Str *part, const char *b, size_t n, void (*fn)(void *ctx, char *line, size_t n), void *ctx);
#endif
