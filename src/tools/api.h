/* A private copy of the declarations asked of thread 0 for include/tools.h
   (project files package-15/tools-h-request.md); deleted when it lands. */
#ifndef TOOLS_API_H
#define TOOLS_API_H
#include "grid.h"
struct Editor;
struct Buf;

enum { PICK_SMART, PICK_GREP, PICK_BUFFERS, PICK_REGISTERS, PICK_EXPLORER };
int  tools_pick(struct Editor *e, int what);
int  tools_key(struct Editor *e, int key);
void tools_draw(struct Editor *e, Grid *g);
#endif
