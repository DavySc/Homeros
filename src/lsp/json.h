/* JSON for the LSP pipe: a reader that indexes a message in place (one
   token per value, jsmn's shape) and the escaping the writer needs.
   Private to src/lsp/; fuzzed by tests/fuzz_lsp_json.c. */
#ifndef LSP_JSON_H
#define LSP_JSON_H
#include <stddef.h>
#include "str.h"

/* type: 'o' object, 'a' array, 's' string (a, z: the bytes between the
   quotes, still escaped), 'p' number, true, false or null (a, z: its
   bytes).  n: members of an object (keys and values are tokens, key
   first) or elements of an array.  end: the token after this value and
   everything inside it. */
typedef struct { char type; int n, end; size_t a, z; } Jtok;

/* Indexes s[0, n) into *t (grown with realloc, *cap its size).  The number
   of tokens, or -1 if s is not one JSON value (or out of memory, or nested
   deeper than 512). */
int  json_parse(const char *s, size_t n, Jtok **t, int *cap);
/* The value of key in object o, or -1. */
int  json_get(const char *s, const Jtok *t, int o, const char *key);
/* Element k of array a, or -1. */
int  json_at(const Jtok *t, int a, int k);
/* An integer value (-1 if i < 0 or it is not one: callers use it for
   versions, ids, lines and columns, which are never negative). */
long json_int(const char *s, const Jtok *t, int i);
int  json_true(const char *s, const Jtok *t, int i);
/* String i decoded into out (which holds t[i].z - t[i].a + 1 bytes: decoding
   never grows a string); its length.  Lone surrogates become U+FFFD. */
size_t json_str(const char *s, const Jtok *t, int i, char *out);

/* a string literal, its length known at compile time */
#define str_lit(s, lit) str_add(s, lit, sizeof lit - 1)
/* p as the inside of a JSON string; what is not UTF-8 goes as one U+FFFD
   per UTF-16 unit lsp_u16 counts for it, so columns agree.  The number of
   such sequences, or -1 when out of memory. */
int  str_addq(Str *s, const char *p, size_t n);
#endif
