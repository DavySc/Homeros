/* The dashboard's half of tools_key and tools_draw (dash.c), called by
   pick.c's while no picker is open (keys) and under the picker (drawing). */
#ifndef DASH_H
#define DASH_H
#include "core.h"
int  dash_key(Editor *e, int k);     /* 1 taken */
void dash_draw(Editor *e, Grid *g);
#endif
