/* The event loop: one thread, poll(2).  Package 10 (src/loop/) adds child
   processes on non-blocking pipes with output queues, the timer, Ctrl-C
   polling inside long commands, and the Windows twin. */
#ifndef LOOP_H
#define LOOP_H
#include <stddef.h>

typedef void (*LoopFn)(void *ctx, int fd);

/* On Windows a watched fd is a waitable HANDLE cut to int (only its low 32
   bits are significant, as Microsoft documents), and LOOP_MSGQ watches the
   thread's message queue (MsgWaitForMultipleObjectsEx with QS_ALLINPUT and
   MWMO_INPUTAVAILABLE, so messages already seen but left queued still wake
   it: measured under Wine by package 9's review, 3 of 3 runs). */
enum { LOOP_MSGQ = -1 };

int  loop_watch(int fd, LoopFn fn, void *ctx);   /* call fn when fd is readable; 0 ok, -1 full */
void loop_unwatch(int fd);
/* Waits up to timeout_ms (-1 forever) and calls the functions of ready fds.
   0 ok (also on a signal), -1 error. */
int  loop_once(int timeout_ms);
/* Is a watched fd readable now?  Runs nothing (syntax_update's stop). */
int  loop_waiting(void);

/* Calls fn once, ms from now; the same fn and ctx replace the earlier timer,
   ms < 0 cancels it.  loop_once never sleeps past the next timer.  0 ok,
   -1 full. */
int  loop_timer(void (*fn)(void *ctx), void *ctx, int ms);
/* The monotonic clock in ns (CLOCK_MONOTONIC; QueryPerformanceCounter on Windows). */
long long loop_now(void);

/* Neovim's got_int and line_breakcheck(): a watch function that reads
   Ctrl-C sets loop_int, whoever runs commands clears it.  A long command
   calls loop_breakcheck, which on every 32nd call polls the watched fds with
   timeout 0 and runs their functions (which only queue input); process pipes
   and timers never run inside a command.  Returns loop_int. */
extern int loop_int;
int  loop_breakcheck(void);

/* Child processes on non-blocking pipes (Neovim's jobstart).  fn gets stdout
   (stream 1) and stderr (2) as they arrive, n = 0 at each one's end, then,
   once both are closed, stream 0 with b = NULL and n = the exit code
   (128 + signal if killed), as Neovim's on_exit.  proc_spawn returns an id
   >= 0, or -1. */
typedef void (*ProcFn)(void *ctx, int stream, const char *b, size_t n);
int  proc_spawn(char *const argv[], const char *dir, ProcFn fn, void *ctx);
/* proc_spawn of a command line through the user's shell, as Neovim's
   'shell': $SHELL -c cmd (sh if unset); on Windows %COMSPEC% /s /c "cmd"
   with the line passed verbatim (shellxquote and libuv's
   VERBATIM_ARGUMENTS), since the CRT's quoting is not cmd.exe's.  For
   :make and the terminal. */
int  proc_shell(const char *cmd, const char *dir, ProcFn fn, void *ctx);
/* The user's shell, the one choice proc_shell and the terminal's proc_pty
   share: $SHELL when set and not empty, else sh; %COMSPEC%, else cmd.exe */
const char *proc_shell_path(void);
int  proc_write(int id, const void *b, size_t n);  /* queued, never blocks; 0 ok, -1 no memory or child gone */
void proc_close(int id);                           /* EOF on stdin once the queue is written */
void proc_kill(int id);                            /* SIGTERM, SIGKILL 2 s later (jobstop) */
/* Neovim's system() (formatprg, conform's sync format): runs only child
   id's pipes (its fn is called as usual) and the watches, as
   loop_breakcheck does (so Ctrl-C arrives), until it is reported,
   deferring every timer and other child; Ctrl-C or ms passing kills it as
   proc_kill does, and it is still reported before the return.  0 reported
   in time, -1 killed (thread 0's, agreed, for the Verilog formatter). */
int  proc_wait(int id, int ms);
#endif
