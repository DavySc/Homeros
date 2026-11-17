/* A stand-in for package 17, to show what git.h's git_key cannot see.
   main.c gives every key to git_key before keys_feed.  This one takes x
   (magit's discard under evil-collection) and does nothing with it. */
#include "core.h"
#include "git.h"

int git_key(Editor *e, int key) { (void)e; return key == 'x'; }
int git_leader(Editor *e, int prefix, int key) { (void)e; (void)prefix; (void)key; return -1; }
int git_sign(const Buf *b, size_t l) { (void)b; (void)l; return 0; }
void git_free(void) {}
