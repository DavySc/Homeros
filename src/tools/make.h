/* :make's key in its own buffer (make.c), for tools_key; a path relative
   to the current directory (pick.c's), for make.c's list too */
#ifndef TOOLS_MAKE_H
#define TOOLS_MAKE_H
#include "core.h"
int make_key(Editor *e, int key);   /* 1 taken */
/* f (n bytes) without the directory cwd (k bytes) and its separator when
   it is inside it, n updated */
const char *path_rel(const char *cwd, size_t k, const char *f, size_t *n);
#endif
