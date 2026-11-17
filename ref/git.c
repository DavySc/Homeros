/* The thin editor has no git (include/git.h). */
#include "core.h"
#include "git.h"

int git_key(Editor *e, int key) { (void)e; (void)key; return 0; }
int git_leader(Editor *e, int prefix, int key) { (void)prefix; (void)key; core_msg(e, "git: not built"); return -1; }
int git_sign(const Buf *b, size_t l) { (void)b; (void)l; return 0; }
void git_free(void) {}
