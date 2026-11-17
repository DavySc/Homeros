/* No grammars: every file is plain text until package 12. */
#include "syntax.h"

Syntax *syntax_open(const char *path, const Text *t) { (void)path; (void)t; return NULL; }
void syntax_free(Syntax *s) { (void)s; }
void syntax_edit(Syntax *s, const Text *t, size_t pos, size_t ndel, const char *ins, size_t nins)
{
	(void)s; (void)t; (void)pos; (void)ndel; (void)ins; (void)nins;
}
int syntax_update(Syntax *s, const Text *t, int (*stop)(void)) { (void)s; (void)t; (void)stop; return 0; }
void syntax_faces(Syntax *s, size_t a, size_t z, unsigned char *face) { (void)s; (void)a; (void)z; (void)face; }
