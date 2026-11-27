#include "session.h"
#include "ask.h"
#include <stdlib.h>
#include <string.h>

/* An ask's event, with the answer when there is a stopped program to look at. n: the command's bytes. */
static void answer(Session *s, const Engine *stopped, uint32_t n)
{
  SessionEvent ev = {.asked = true, .stale = !stopped};
  const uint64_t *ask = s->command + sizeof(SessionCommand) / 8;
  uint64_t *to = s->event + sizeof ev / 8;
  if (n >= sizeof(SessionCommand) + sizeof(Ask)) ev.tag = ((const Ask *)ask)->tag;
  if (stopped) ev.answer = ask_answer(stopped, ask, n - (uint32_t)sizeof(SessionCommand), to, QUEUE_BYTES - 8 - sizeof ev);   /* a message has at most QUEUE_BYTES - 4 bytes */
  memcpy(s->event, &ev, sizeof ev);
  queue_push(&s->events, s->event, (uint32_t)sizeof ev + ev.answer, true);
}

/* The engine asks between its waits for the target (engine.h, halt): a halt or a quit stops it, an ask is answered
   without a look at the target. Anything else the editor sends while the target runs is dropped (Halt.tla: it does not). */
static bool halt(void *arg)
{
  Session *s = arg;
  SessionCommand c;
  for (uint32_t n; !s->quit && (n = queue_pop(&s->commands, s->command, sizeof s->command, false));) {
    memcpy(&c, s->command, sizeof c);
    s->quit = c.kind == SESSION_QUIT;
    if (c.kind == SESSION_HALT || s->quit) return true;
    if (c.kind == SESSION_ASK) answer(s, NULL, n);
  }
  return s->quit;
}

/* The engine asks when a thread comes to a breakpoint (engine.h, pass): the breakpoint's condition, where the run was
   sent one, says no. */
static bool pass(void *arg, const void *engine, uint32_t thread, uint64_t addr)
{
  const Session *s = arg;
  const char *text = (const char *)(s->run + s->run_count), *end = (const char *)s->run + s->run_bytes, *next;
  for (uint32_t i = 0; i < s->run_count && text < end && (next = memchr(text, 0, (size_t)(end - text))); i += 1, text = next + 1)
    if (s->run[i] == addr) return ask_zero(engine, thread, text);
  return false;
}

static void *session(void *arg)
{
  Session *s = arg;
  Engine e;
  bool open = false;
  while (!s->quit) {
    SessionCommand c;
    SessionEvent ev = {0};
    uint32_t n = queue_pop(&s->commands, s->command, sizeof s->command, true);
    const void *more = s->command + sizeof c / 8;
    memcpy(&c, s->command, sizeof c);
    s->quit = c.kind == SESSION_QUIT;
    if (c.kind == SESSION_OPEN && !open && n > sizeof c) {
      ((char *)s->command)[n - 1] = 0;
      open = engine_open(&e, more, 0, &ev.stop.thread);
      ev.stop.kind = open ? TARGET_MODULE : TARGET_EXIT, ev.stop.addr = open && e.module_count ? e.modules[0].image.base : 0;
      if (open) e.halt = halt, e.halt_arg = e.pass_arg = s, e.pass = pass;
    } else if (c.kind == SESSION_RUN && open && c.count <= (n - sizeof c) / 8) {
      memcpy(s->run, more, s->run_bytes = n - sizeof c), s->run_count = c.count;   /* an ask on the way takes the command's place */
      ev.stop = engine_run(&e, (int)c.how, c.thread, s->run, c.count);
      ev.stop.text = 0;
      if (ev.stop.kind == TARGET_EXIT) engine_close(&e), open = false;
    } else {   /* an ask; or a halt when nothing runs (the target had stopped by itself), or a command that makes no sense now */
      if (c.kind == SESSION_ASK) answer(s, open ? &e : NULL, n);
      continue;
    }
    queue_push(&s->events, &ev, sizeof ev, true);
  }
  if (open) engine_close(&e);
  queue_push(&s->events, &(SessionEvent){.gone = true}, sizeof(SessionEvent), true);
  return 0;
}

void session_start(Session *s, void (*wake)(void *), void *wake_arg)
{
  queue_init(&s->commands, 0, 0), queue_init(&s->events, wake, wake_arg);
  s->quit = false;
  if (pthread_create(&s->thread, 0, session, s)) abort();
}
