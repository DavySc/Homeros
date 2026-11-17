/* The thin editor has no daily tools (include/tools.h). */
#include "core.h"
#include "tools.h"

int tools_pick(Editor *e, int what) { (void)what; core_msg(e, "tools: not built"); return -1; }
int tools_key(Editor *e, int key) { (void)e; (void)key; return 0; }
void tools_draw(Editor *e, Grid *g) { (void)e; (void)g; }
int tools_oil(Editor *e, const char *dir) { (void)dir; core_msg(e, "tools: not built"); return -1; }
int tools_oil_key(Editor *e, int key) { (void)e; (void)key; return 0; }
int tools_write(Editor *e, Buf *b) { (void)b; core_msg(e, "tools: not built"); return -1; }
