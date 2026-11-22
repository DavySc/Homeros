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
bool target_write(Target *t, uint64_t addr, const void *bytes, uint32_t n) { return own(t, false), (void)addr, (void)bytes, (void)n, false; }
bool target_regs(Target *t, uint32_t thread, TargetRegs *regs) { return own(t, false), *regs = (TargetRegs){.rip = t->rip}, thread == 1; }
uint32_t target_threads(const Target *t, uint32_t *ids, uint32_t room) { return (void)t, (void)ids, (void)room, 0; }
void target_close(Target *t) { own(t, false), free(t); }
/* A made-up answer: the ask's own bytes after its head (at most 16; for "full" as many f as there is room for), read from the target's side: only between two runs. */
uint32_t ask_answer(const Engine *e, const void *ask, uint32_t n, void *out, uint32_t cap)
{
  uint32_t more = n < sizeof(Ask) ? 0 : n - (uint32_t)sizeof(Ask) < 16 ? n - (uint32_t)sizeof(Ask) : 16;
  own(e->target, false);
  if (cap < 16) abort();
  memcpy(out, (const char *)ask + sizeof(Ask), more);
  if (more == 5 && !memcmp(out, "full", 5)) memset(out, 'f', more = cap);   /* all the room there is */
  return more;
}

static Session s;
static int fd;
static void wake(void *arg) { uint64_t one = 1; (void)arg; if (write(fd, &one, sizeof one) < 0) abort(); }

static void send(uint32_t kind, uint64_t breakpoint, const char *text)
{
  struct { SessionCommand c; char more[8]; } m = {{.kind = kind, .how = ENGINE_RUN, .thread = 1, .count = breakpoint != 0}, {0}};
  memcpy(m.more, text ? (const void *)text : (const void *)&breakpoint, 8);
  if (!queue_push(&s.commands, &m, sizeof m.c + (text || breakpoint ? 8 : 0), false)) abort();
}
/* An ask with this tag and text (up to 15 characters); with no text, one cut short of its head. */
static void ask(uint32_t tag, const char *text)
{
  struct { SessionCommand c; Ask a; char more[16]; } m = {{.kind = SESSION_ASK}, {.tag = tag}, {0}};
  if (text) strcpy(m.more, text);
  if (!queue_push(&s.commands, &m, text ? sizeof m.c + sizeof m.a + (uint32_t)strlen(text) + 1 : sizeof m.c + 8, false)) abort();
}
/* The editor's side: clear the wake, then take. False when nothing comes in ms. */
static char answered[QUEUE_BYTES];   /* what came behind the last event */
static bool take(SessionEvent *ev, int ms)
{
  static struct { SessionEvent ev; char answer[QUEUE_BYTES]; } m;
  for (;;) {
    uint64_t n;
    uint32_t got = queue_pop(&s.events, &m, sizeof m, false);
    if (got) {
      *ev = m.ev, memcpy(answered, m.answer, m.ev.answer);
      if (got != sizeof m.ev + m.ev.answer) abort();
      return true;
    }
    if (poll(&(struct pollfd){fd, POLLIN, 0}, 1, ms) == 0) return false;
    if (read(fd, &n, sizeof n) < 0) abort();
  }
}
/* The last event is the answer to the ask with this tag and text: the text again; stale: nothing but the tag. */
static bool is_answer(const SessionEvent *ev, uint32_t tag, const char *text, bool stale)
{
  return ev->asked && !ev->gone && ev->tag == tag && ev->stale == stale && ev->answer == (stale || !text ? 0 : strlen(text) + 1) && (stale || !text || !strcmp(answered, text));
}
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (double)t.tv_sec * 1e3 + (double)t.tv_nsec / 1e6; }

#define FAIL(...) return printf("test_session: round %u: ", round), printf(__VA_ARGS__), printf("\n"), 1

int main(void)
{
  uint32_t seed = 1, round = 0, halted = 0, trapped = 0, races = 0, raced = 0, asks = 0, late = 0;
  double worst = 0;
  SessionEvent ev;
  fd = eventfd(0, EFD_NONBLOCK);
  session_start(&s, wake, 0);
  ask(77, "early");   /* no program yet: nothing to look at */
  if (!take(&ev, 5000) || !is_answer(&ev, 77, "early", true)) FAIL("an ask before a program is open is not answered as stale");
  send(SESSION_OPEN, 0, "program");
  if (!take(&ev, 5000) || ev.stop.kind != TARGET_MODULE || ev.stop.thread != 1 || ev.asked) FAIL("the program did not open");
  send(SESSION_OPEN, 0, "program");
  if (take(&ev, 30)) FAIL("a second program was opened beside the first");
  for (; round < 400; round += 1) {
    uint32_t what = (seed = seed * 1664525u + 1013904223u) >> 28;   /* 0..15 */
    if (what < 2) send(SESSION_HALT, 0, 0);   /* a halt when nothing runs is nothing */
    /* stopped: every ask has its answer, in order, a cut one an empty answer */
    ask(round, "first"), ask(0, NULL), ask(round + 1000, "second");
    if (!take(&ev, 5000) || !is_answer(&ev, round, "first", false) || !take(&ev, 5000) || !is_answer(&ev, 0, NULL, false) || !take(&ev, 5000) || !is_answer(&ev, round + 1000, "second", false)) FAIL("three asks are not answered in turn");
    ask(round, "full");   /* an answer as long as a message can carry */
    if (!take(&ev, 5000) || !ev.asked || ev.stale || ev.answer < QUEUE_BYTES - 16 - sizeof ev || strspn(answered, "f") < ev.answer) FAIL("a full answer has %u bytes", ev.answer);
    asks += 4;
    if (what < 6) {   /* to the breakpoint */
      send(SESSION_RUN, 0x401000 + what, 0);
      if (!take(&ev, 5000) || ev.stop.kind != TARGET_TRAP || ev.stop.addr != 0x401000 + what) FAIL("no stop on the breakpoint");
      trapped += 1;
    } else if (what < 11) {   /* a halt races the breakpoint: one stop, whichever */
      /* and an ask races both: answered as stale before the stop, or with an answer after it */
      send(SESSION_RUN, 0x402000, 0), usleep(what * 60), ask(round, "race"), send(SESSION_HALT, 0, 0);
      SessionEvent other;
      if (!take(&ev, 5000) || !take(&other, 5000)) FAIL("no stop, or no answer");
      if (ev.asked ? !is_answer(&ev, round, "race", true) : !other.asked || other.stale || other.tag != round || other.answer != 5) FAIL("an ask during a run and its stop came in an order that cannot be");
      late += !ev.asked, asks += 1;
      if (ev.asked) ev = other;
      if (ev.stop.kind != TARGET_TRAP && ev.stop.kind != TARGET_HALT) FAIL("no stop");
      races += 1, raced += ev.stop.kind == TARGET_HALT;
    }
    /* a run that only a halt ends: the halt sent before it (if the breakpoint won above, it is still in the queue) does not end it */
    send(SESSION_RUN, 0, 0);
    if (take(&ev, 30)) FAIL("a run without breakpoints stopped by itself (event %d)", ev.stop.kind);
    ask(round, "running");   /* answered at once, without a look at the target (the made-up one ends the test if it is looked at) */
    if (!take(&ev, 5000) || !is_answer(&ev, round, "running", true)) FAIL("an ask while the target runs is not answered as stale");
    asks += 1;
    double at = now();
    send(SESSION_HALT, 0, 0);
    if (!take(&ev, 5000) || ev.stop.kind != TARGET_HALT) FAIL("a halt did not stop the target");
    if (now() - at > 250) FAIL("a halt took %.0f ms", now() - at);
    worst = now() - at > worst ? now() - at : worst, halted += 1;
  }
  /* the program ends: nothing runs until it is opened again */
  send(SESSION_RUN, GONE, 0);
  if (!take(&ev, 5000) || ev.stop.kind != TARGET_EXIT) FAIL("no event for the program's end");
  ask(78, "ended");
  if (!take(&ev, 5000) || !is_answer(&ev, 78, "ended", true)) FAIL("an ask after the program's end is not answered as stale");
  send(SESSION_RUN, 0x401000, 0), send(SESSION_OPEN, 0, "program");
  if (!take(&ev, 5000) || ev.stop.kind != TARGET_MODULE) FAIL("the ended program ran, or did not open again");
  send(SESSION_RUN, 0, 0), send(SESSION_QUIT, 0, 0);
  if (!take(&ev, 5000) || ev.stop.kind != TARGET_HALT || ev.gone || !take(&ev, 5000) || !ev.gone || take(&ev, 30)) FAIL("a quit did not stop the run and end the thread");
  pthread_join(s.thread, 0);
  if (!raced || raced == races) FAIL("halts that won their race with a breakpoint: %u of %u", raced, races);
  if (!late || late == races) FAIL("asks that came after the stop they raced: %u of %u", late, races);
  printf("test_session: %u stops on a breakpoint, %u halts (and %u of %u that raced a breakpoint), longest halt %.1f ms; %u asks answered (%u of %u that raced a stop after it)\n", trapped, halted, raced, races, worst, asks, late, races);
  return 0;
}
