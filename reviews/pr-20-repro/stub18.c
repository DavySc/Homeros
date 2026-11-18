/* Review scratch: what PR 18's core.h adds and src/core does not have yet,
   so that PR 20 merged with PR 18 links. */
#include "core.h"
int core_changed;
void core_setpcmark(Editor *e) { (void)e; }
