/* Lua patterns, as LuaJIT 2.1's string.find (Lua 5.1 with %g), the engine of
   Neovim's #lua-match? predicate.  The subject is bytes [a, z) read through
   at(ctx, i); the pattern is p[0..n). */
#ifndef LMATCH_H
#define LMATCH_H
#include <stddef.h>

/* 1 if the pattern occurs in the subject (anchored by a leading ^). */
int lmatch_find(int (*at)(void *ctx, size_t i), void *ctx, size_t a, size_t z,
                const char *p, size_t n);
/* The subject with each match replaced by r[0..rn) as string.gsub reads it,
   into out[0..cap); its length, or -1 (a pattern Lua rejects, or past cap). */
long lmatch_gsub(int (*at)(void *ctx, size_t i), void *ctx, size_t a, size_t z,
                 const char *p, size_t n, const char *r, size_t rn, char *out, size_t cap);
#endif
