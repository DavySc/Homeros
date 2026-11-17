/* A stand-in grammar: byte at offset p gets face 1 + p % 10, so every face
   and every byte offset core_draw asks for can be checked. */
#include <stdlib.h>
#include "syntax.h"
struct Syntax { size_t len; };
size_t fake_asked_max, fake_calls, fake_bytes;
Syntax *syntax_open(const char *path, const Text *t) { (void)path; Syntax *s = malloc(sizeof *s); s->len = text_len(t); return s; }
void syntax_free(Syntax *s) { free(s); }
void syntax_edit(Syntax *s, const Text *t, size_t pos, size_t ndel, const char *ins, size_t nins)
{ (void)t; (void)pos; (void)ins; s->len = s->len - ndel + nins; }
int syntax_update(Syntax *s, const Text *t, int (*stop)(void)) { (void)s; (void)t; (void)stop; return 0; }
void syntax_faces(Syntax *s, size_t a, size_t z, unsigned char *face)
{
	if (a > z || z > s->len) abort();           /* asked outside the text */
	fake_calls++; fake_bytes += z - a;
	for (size_t p = a; p < z; p++) face[p - a] = (unsigned char)(1 + p % 10);
}
