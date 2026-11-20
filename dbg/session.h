/* The engine thread: the one thread that touches a debugged program, and its two queues, which are
   all the editor shares with it (queue.h; knowledge base 09 and lessons L13, L14; specs/Halt.tla).
   The editor pushes commands (wait = false) and pops events when the events queue wakes it. */
#ifndef DBG_SESSION_H
#define DBG_SESSION_H
#include "queue.h"
#include "target.h"

enum { SESSION_OPEN,   /* start a program: the command line follows, with its 0 */
       SESSION_RUN,    /* as engine_run: `how`, `thread`, and `count` breakpoint addresses follow. Sent only after the
                          stop event of the run before has been taken. */
       SESSION_HALT,   /* stop the target now, if it runs */
       SESSION_QUIT }; /* end the program and the thread */
typedef struct { uint32_t kind, how, thread, count; } SessionCommand;

/* One event for every SESSION_OPEN (kind TARGET_MODULE: the program's base and first thread; TARGET_EXIT: it did not
   start) and every SESSION_RUN (as engine_run returns; TARGET_HALT after a halt); `text` is not carried. The
   thread's last event has `gone` set: the editor joins it then. */
typedef struct { TargetEvent stop; bool gone; } SessionEvent;

typedef struct { Queue commands, events; pthread_t thread; bool quit; uint64_t command[QUEUE_BYTES / 8]; } Session;   /* command: the one being read */

/* wake: called on the engine thread after each event is pushed. */
void session_start(Session *s, void (*wake)(void *), void *wake_arg);
#endif
