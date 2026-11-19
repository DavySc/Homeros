#include "loop.h"
#include "gitint.h"
int git_counts(Repo *r, const char *p) { (void)r; (void)p; return 1; }
void git_schedule(Repo *r) { (void)r; }
int loop_watch(int fd, LoopFn fn, void *ctx) { (void)fd; (void)fn; (void)ctx; return 0; }
void loop_unwatch(int fd) { (void)fd; }
