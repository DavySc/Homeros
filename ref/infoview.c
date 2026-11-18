/* The thin editor has no infoview (include/bridge.h). */
#include "bridge.h"
#include "core.h"

int  bridge_open(struct Editor *e) { core_msg(e, "No infoview in this build"); return -1; }
void bridge_update(struct Editor *e) { (void)e; }
void bridge_free(void) {}
