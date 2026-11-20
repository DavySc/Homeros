/* The engine thread and its queues (dbg/session.c, specs/Halt.tla) on a made-up target, so that it runs here under
   the sanitizers: every run command is answered by one stop event, a halt stops the run it was sent during and no
   later one, a quit ends the thread, and only the engine thread ever touches the target (the made-up target
   aborts on a call from another thread, and on a call that makes no sense in its state).
   The made-up target: one thread; running, it stops on its first trap after three waits (a trap at GONE: the
   program ends there), or after one step. */
#include "../dbg/engine.c"
#include "../dbg/session.c"
#include <poll.h>
#include <stdio.h>
#include <sys/eventfd.h>
#include <time.h>
#include <unistd.h>

enum { GONE = 0xdead };
struct Target { pthread_t owner; bool running; uint64_t trap, rip; uint32_t step, waits; };

static void own(Target *t, bool running)
{
  if (pthread_equal(pthread_self(), t->owner) && t->running == running) return;
  fprintf(stderr, "test_session: the target is called from another thread, or in the wrong state\n");
  abort();
}

Target *target_open(const char *command_line, uint32_t pid, TargetEvent *first)
{
  Target *t = calloc(1, sizeof *t);
  if (!t || pid || strcmp(command_line, "program")) abort();
  t->owner = pthread_self();
  *first = (TargetEvent){.kind = TARGET_MODULE, .thread = 1, .addr = 0x400000, .text = "program"};
  return t;
}
void target_resume(Target *t, const uint64_t *traps, uint32_t count, uint32_t step_thread)
{
  own(t, false);
  t->running = true, t->trap = count ? traps[0] : 0, t->step = step_thread, t->waits = 0;
}
bool target_wait(Target *t, uint32_t ms, TargetEvent *e)
{
  own(t, true);
  usleep(ms * 20);
  *e = (TargetEvent){.kind = TARGET_RUNNING, .thread = 1};
  if (t->step) e->kind = TARGET_STEP, e->addr = t->rip += 1;
  else if (t->trap && (t->waits += 1) == 3) e->kind = t->trap == GONE ? TARGET_EXIT : TARGET_TRAP, e->addr = t->rip = t->trap;
  return !(t->running = e->kind == TARGET_RUNNING);
}
void target_halt(Target *t) { own(t, true), t->running = false; }
void target_swallow(Target *t) { own(t, false); }
bool target_read(Target *t, uint64_t addr, void *bytes, uint32_t n) { return own(t, false), (void)addr, (void)bytes, (void)n, false; }
bool target_regs(Target *t, uint32_t thread, TargetRegs *regs) { return own(t, false), *regs = (TargetRegs){.rip = t->rip}, thread == 1; }
void target_close(Target *t) { own(t, false), free(t); }

static Session s;
static int fd;
static void wake(void *arg) { uint64_t one = 1; (void)arg; if (write(fd, &one, sizeof one) < 0) abort(); }

static void send(uint32_t kind, uint64_t breakpoint, const char *text)
{
  struct { SessionCommand c; char more[8]; } m = {{.kind = kind, .how = ENGINE_RUN, .thread = 1, .count = breakpoint != 0}, {0}};
  memcpy(m.more, text ? (const void *)text : (const void *)&breakpoint, 8);
  if (!queue_push(&s.commands, &m, sizeof m.c + (text || breakpoint ? 8 : 0), false)) abort();
}
/* The editor's side: clear the wake, then take. False when nothing comes in ms. */
static bool take(SessionEvent *ev, int ms)
{
  for (;;) {
    uint64_t n;
    if (queue_pop(&s.events, ev, sizeof *ev, false)) return true;
    if (poll(&(struct pollfd){fd, POLLIN, 0}, 1, ms) == 0) return false;
    if (read(fd, &n, sizeof n) < 0) abort();
  }
}
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (double)t.tv_sec * 1e3 + (double)t.tv_nsec / 1e6; }

#define FAIL(...) return printf("test_session: round %u: ", round), printf(__VA_ARGS__), printf("\n"), 1

int main(void)
{
  uint32_t seed = 1, round = 0, halted = 0, trapped = 0, races = 0, raced = 0;
  double worst = 0;
  SessionEvent ev;
  fd = eventfd(0, EFD_NONBLOCK);
  session_start(&s, wake, 0);
  send(SESSION_OPEN, 0, "program");
  if (!take(&ev, 5000) || ev.stop.kind != TARGET_MODULE || ev.stop.thread != 1) FAIL("the program did not open");
  for (; round < 400; round += 1) {
    uint32_t what = (seed = seed * 1664525u + 1013904223u) >> 28;   /* 0..15 */
    if (what < 2) send(SESSION_HALT, 0, 0);   /* a halt when nothing runs is nothing */
    if (what < 6) {   /* to the breakpoint */
      send(SESSION_RUN, 0x401000 + what, 0);
      if (!take(&ev, 5000) || ev.stop.kind != TARGET_TRAP || ev.stop.addr != 0x401000 + what) FAIL("no stop on the breakpoint");
      trapped += 1;
    } else if (what < 11) {   /* a halt races the breakpoint: one stop, whichever */
      send(SESSION_RUN, 0x402000, 0), usleep(what * 60), send(SESSION_HALT, 0, 0);
      if (!take(&ev, 5000) || (ev.stop.kind != TARGET_TRAP && ev.stop.kind != TARGET_HALT)) FAIL("no stop");
      races += 1, raced += ev.stop.kind == TARGET_HALT;
    }
    /* a run that only a halt ends: the halt sent before it (if the breakpoint won above, it is still in the queue) does not end it */
    send(SESSION_RUN, 0, 0);
    if (take(&ev, 30)) FAIL("a run without breakpoints stopped by itself (event %d)", ev.stop.kind);
    double at = now();
    send(SESSION_HALT, 0, 0);
    if (!take(&ev, 5000) || ev.stop.kind != TARGET_HALT) FAIL("a halt did not stop the target");
    if (now() - at > 250) FAIL("a halt took %.0f ms", now() - at);
    worst = now() - at > worst ? now() - at : worst, halted += 1;
  }
  /* the program ends: nothing runs until it is opened again */
  send(SESSION_RUN, GONE, 0);
  if (!take(&ev, 5000) || ev.stop.kind != TARGET_EXIT) FAIL("no event for the program's end");
  send(SESSION_RUN, 0x401000, 0), send(SESSION_OPEN, 0, "program");
  if (!take(&ev, 5000) || ev.stop.kind != TARGET_MODULE) FAIL("the ended program ran, or did not open again");
  send(SESSION_RUN, 0, 0), send(SESSION_QUIT, 0, 0);
  if (!take(&ev, 5000) || ev.stop.kind != TARGET_HALT || ev.gone || !take(&ev, 5000) || !ev.gone || take(&ev, 30)) FAIL("a quit did not stop the run and end the thread");
  pthread_join(s.thread, 0);
  if (!raced || raced == races) FAIL("halts that won their race with a breakpoint: %u of %u", raced, races);
  printf("test_session: %u stops on a breakpoint, %u halts (and %u of %u that raced a breakpoint), longest halt %.1f ms\n", trapped, halted, raced, races, worst);
  return 0;
}
