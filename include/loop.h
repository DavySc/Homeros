/* The event loop: one thread, poll(2).  Package 10 (src/loop/) adds child
   processes on non-blocking pipes with output queues, the timer, Ctrl-C
   polling inside long commands, and the Windows twin. */
#ifndef LOOP_H
#define LOOP_H

typedef void (*LoopFn)(void *ctx, int fd);

int  loop_watch(int fd, LoopFn fn, void *ctx);   /* call fn when fd is readable; 0 ok, -1 full */
void loop_unwatch(int fd);
/* Waits up to timeout_ms (-1 forever) and calls the functions of ready fds.
   0 ok (also on a signal), -1 error. */
int  loop_once(int timeout_ms);
#endif
