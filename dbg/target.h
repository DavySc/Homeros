/* Process control: the one place the debugger touches the operating system's debug interface
   (specs/Run.tla). One target process; every call comes from the engine thread, the thread
   that opened the target (Windows gives a process's debug events only to that thread).

   The target is stopped or running. While it is stopped its memory holds the program's own
   bytes: target_resume writes the breakpoint instructions ("traps") and every stop takes them
   out again, so nothing remembers between runs what is planted. */
#ifndef DBG_TARGET_H
#define DBG_TARGET_H
#include <stdbool.h>
#include <stdint.h>

enum { TARGET_RUNNING,                                   /* nothing happened in the time given */
       TARGET_THREAD, TARGET_THREAD_GONE,                /* noted while it runs on: a thread began or ended, */
       TARGET_MODULE, TARGET_MODULE_GONE,                /*   a module was mapped at addr (`text`: its file's path, good until the next wait) or unmapped, */
       TARGET_OUTPUT,                                    /*   the program wrote `code` bytes of debug text (`text`, good until the next wait), */
       TARGET_LATE,                                      /*   the event of a trap or a step of an earlier run came only now: nothing to stop for
                                                              (code 1: its thread still stood behind the trap byte and was put back) */
       TARGET_TRAP, TARGET_BREAK, TARGET_STEP,           /* stopped: on a trap at addr, after a breakpoint of the program's own before addr, after one instruction, */
       TARGET_EXCEPTION, TARGET_EXIT };                  /*   by exception `code` at addr (`unhandled`: the program's handlers have seen it and none took it),
                                                              or ended with exit code `code` */
typedef struct { int kind; uint32_t thread; uint64_t addr, code; const char *text; bool unhandled; } TargetEvent;

/* in the order of the instruction encoding, which is also RDI's register codes 1 to 16 */
typedef struct { uint64_t gp[16], rip, rflags; } TargetRegs;

typedef struct Target Target;

/* Starts command_line under the debugger, or attaches to process `pid` when command_line is
   null. The target is stopped before its first instruction (attached: wherever it was).
   first: the program itself, as a TARGET_MODULE event. */
Target *target_open(const char *command_line, uint32_t pid, TargetEvent *first);
/* Plants the traps and lets the target run; or, with step_thread, lets that thread alone run one
   instruction (alone, so that no other thread passes a place whose trap is out for the step). */
void target_resume(Target *t, const uint64_t *traps, uint32_t count, uint32_t step_thread);
/* While stopped by an exception: the program will not get it (else its handlers do, on the resume); the
   instruction that raised it runs again (RAD: ignore_previous_exception). */
void target_swallow(Target *t);
/* While running: waits up to ms for something to happen. True once the target is stopped. */
bool target_wait(Target *t, uint32_t ms, TargetEvent *e);
/* While running: stops every thread where it is. */
void target_halt(Target *t);
/* While stopped. False when the memory or the thread is not there. */
typedef bool TargetRead(Target *t, uint64_t addr, void *bytes, uint32_t n);   /* for code that tests run on memory of their own */
TargetRead target_read;
bool target_write(Target *t, uint64_t addr, const void *bytes, uint32_t n);
bool target_regs(Target *t, uint32_t thread, TargetRegs *regs);
/* Whether the thread has run into a trap without a TARGET_TRAP stop for it yet (another thread's stop came first).
   It stands on the trap's address and has run nothing since: a run that plants the trap again stops for it.
   (A thread that ran on, the trap not planted, and has come to stand on the same address again counts too:
   nothing tells the two apart.) */
bool target_owed(Target *t, uint32_t thread);
/* Ends the target process and frees t. */
void target_close(Target *t);
#endif
