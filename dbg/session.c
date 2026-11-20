#include "session.h"
#include "engine.h"
#include <stdlib.h>
#include <string.h>

/* The engine asks between its waits for the target (engine.h, halt): a halt or a quit stops it. Anything else the
   editor sends while the target runs is dropped (Halt.tla: it does not). */
static bool halt(void *arg)
{
  Session *s = arg;
  SessionCommand c;
  while (!s->quit && queue_pop(&s->commands, s->command, sizeof s->command, false)) {
    memcpy(&c, s->command, sizeof c);
    s->quit = c.kind == SESSION_QUIT;
    if (c.kind == SESSION_HALT || s->quit) return true;
  }
  return s->quit;
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
      if (open) e.halt = halt, e.halt_arg = s;
    } else if (c.kind == SESSION_RUN && open && c.count <= (n - sizeof c) / 8) {
      ev.stop = engine_run(&e, (int)c.how, c.thread, more, c.count);
      ev.stop.text = 0;
      if (ev.stop.kind == TARGET_EXIT) engine_close(&e), open = false;
    } else continue;   /* a halt when nothing runs (the target had stopped by itself), or a command that makes no sense now */
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
