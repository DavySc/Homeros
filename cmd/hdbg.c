/* hdbg <command line>: the debugger at a console. It is the editor's side of the engine thread (dbg/session.h, dbg/ask.h)
   with a line where the editor has a key, so it calls nothing of the engine and sees only what the two queues carry.
     b <file:line or function> [if <e>]   a breakpoint there, which stops only where the expression is not 0; taken away when there is one
     c [ms]                      run on (and stop the program after ms when it has not stopped by then)
     s  n  o  si  ni             step into a line, over a line, out of the function, into and over one instruction
     bt   f <n>                  the stack; look at its frame n
     t                           the threads and where each is
     l   r   p <e>   x <e>       the frame's variables, its registers, an expression, an expression with its parts
     q                           end the program */
#include "../dbg/ask.h"
#include "../dbg/session.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

static Session s;
static HANDLE woke;
static union { uint64_t align[QUEUE_BYTES / 8]; SessionEvent ev; } in;   /* the event last taken, with its answer */
static uint64_t breakpoints[64];
static char conditions[64][128];
static uint32_t count, thread, frame;
static bool alive;

static void wake(void *arg) { (void)arg, SetEvent(woke); }
static void push(const void *m, size_t n) { if (!queue_push(&s.commands, m, (uint32_t)n, false)) abort(); }   /* one command at a time: there is room */

/* The next event: every command sent here has one. */
static const SessionEvent *take(uint32_t ms)
{
  while (!queue_pop(&s.events, &in, sizeof in, false))
    if (WaitForSingleObject(woke, ms) == WAIT_TIMEOUT) push(&(SessionCommand){.kind = SESSION_HALT}, sizeof(SessionCommand)), ms = INFINITE;
  return &in.ev;
}

/* The answer to the ask with the n bytes it brings; its length to *got. */
static const void *ask(Ask a, const void *more, size_t n, uint32_t *got)
{
  struct { SessionCommand c; Ask a; char more[sizeof(View) + 1024]; } m = {.c.kind = SESSION_ASK, .a = a};
  memcpy(m.more, more, n);
  push(&m, sizeof m - sizeof m.more + n);
  *got = take(INFINITE)->answer;
  return &in.ev + 1;
}

/* Writes the thread's frames from `first` on, at most `most`; of one alone also its line of the source, when the file is here. */
static void frames(uint32_t of, uint32_t first, uint32_t most)
{
  for (uint32_t n = 1, shown = 0; n && shown < most; first += n) {
    const AskFrame *f = ask((Ask){.kind = ASK_STACK, .thread = of, .first = first, .most = most}, "", 0, &n);
    n /= sizeof *f, shown += n;
    for (uint32_t i = 0; i < n; i += 1) {
      const char *module = f[i].module + strlen(f[i].module);
      while (module > f[i].module && module[-1] != '\\' && module[-1] != '/') module -= 1;
      printf("#%u 0x%llx %s  %s %s:%u\n", first + i, (unsigned long long)f[i].addr, f[i].name, module, f[i].file, f[i].line);
    }
    FILE *source = most == 1 && n ? fopen(f->file, "r") : NULL;
    char text[1000] = "";
    uint32_t line = 0;
    while (source && line < f->line && fgets(text, sizeof text, source)) line += strchr(text, '\n') != NULL;
    if (source && line == f->line) fputs(text, stdout);
    if (source) fclose(source);
  }
}

/* The rows the ask is answered with; how many to *n. */
static const AskRow *table(Ask a, const void *more, size_t bytes, uint32_t *n)
{
  const char *answer = ask(a, more, bytes, n);
  *n = *n > 8 ? (*n - 8) / (uint32_t)sizeof(AskRow) : 0;
  return (const AskRow *)(answer + 8);
}

/* Writes a table of the frame looked at: of the expression, with its parts when `open`, or of what `roots` says. */
static void rows(uint32_t roots, const char *text, bool open)
{
  struct { View view; char text[1024]; } more = {0};
  size_t bytes = sizeof more.view + (roots == ASK_TEXTS ? (size_t)snprintf(more.text, sizeof more.text, "%s", text) + 1 : 0);
  Ask a = {.kind = ASK_ROWS, .thread = thread, .frame = frame, .roots = roots, .most = ASK_MOST};
  uint32_t n;
  const AskRow *row = table(a, &more, bytes, &n);
  if (open && n) view_toggle(&more.view, &row->row);   /* which rows are open is the editor's to keep (ask.h) */
  for (; (row = table(a, &more, bytes, &n)), n; a.first += n)
    for (uint32_t i = 0; i < n; i += 1) printf("%*s%s = %s  (%s)\n", (int)row[i].row.depth * 2, "", row[i].row.text + row[i].row.label, row[i].value, row[i].type);
}

/* What a run or the start ended in, and where the thread that stopped stands. */
static void report(const SessionEvent *ev)
{
  static const char *const why[] = {[TARGET_RUNNING] = "nothing ran: the thread is gone", [TARGET_MODULE] = "started", [TARGET_TRAP] = "breakpoint", [TARGET_BREAK] = "the program's own breakpoint", [TARGET_STEP] = "stepped",
                                    [TARGET_HALT] = "halted", [TARGET_EXCEPTION] = "exception", [TARGET_EXIT] = "ended with"};
  alive = ev->stop.kind != TARGET_EXIT, frame = 0;
  if (ev->stop.thread) thread = ev->stop.thread;   /* a halt is no thread's: the one looked at stays */
  fputs(why[ev->stop.kind], stdout);
  if (ev->stop.kind == TARGET_EXCEPTION || !alive) printf(" 0x%llx%s", (unsigned long long)ev->stop.code, ev->stop.unhandled ? ", which no handler took" : "");
  putchar('\n');
  if (alive) frames(thread, 0, 1);
}

int main(int argc, char **argv)
{
  static const char *const runs[] = {[ENGINE_RUN] = "c", [ENGINE_STEP_INTO_INST] = "si", [ENGINE_STEP_OVER_INST] = "ni", [ENGINE_STEP_INTO_LINE] = "s", [ENGINE_STEP_OVER_LINE] = "n", [ENGINE_STEP_OUT] = "o"};
  struct { SessionCommand c; union { char text[sizeof breakpoints + sizeof conditions]; uint64_t at[64]; }; } m = {.c.kind = SESSION_OPEN};
  char line[1000];
  size_t used = 0;
  for (int i = 1; i < argc; i += 1) used += (size_t)snprintf(m.text + used, used < sizeof m.text ? sizeof m.text - used : 0, i > 1 ? " %s" : "%s", argv[i]);
  if (used == 0 || used >= 1000) return fputs("usage: hdbg <command line>\n", stderr), 2;
  woke = CreateEventA(NULL, FALSE, FALSE, NULL);
  session_start(&s, wake, NULL);
  push(&m, sizeof m.c + used + 1);
  const SessionEvent *ev = take(INFINITE);
  bool started = ev->stop.kind != TARGET_EXIT;
  if (started) report(ev);
  else fputs("hdbg: the program does not start\n", stderr);
  while (alive && fgets(line, sizeof line, stdin)) {
    uint32_t how = 0, n;
    char *more = line + strcspn(line, " \r\n"), end = *more;
    *more = 0, more += end == ' ', more[strcspn(more, "\r\n")] = 0;
    while (how < sizeof runs / sizeof *runs && strcmp(line, runs[how])) how += 1;
    if (how < sizeof runs / sizeof *runs) {
      m.c = (SessionCommand){SESSION_RUN, how, thread, count}, memcpy(m.at, breakpoints, sizeof breakpoints);
      used = count * 8;
      for (uint32_t i = 0; i < count; i += 1) used += (size_t)sprintf(m.text + used, "%s", conditions[i]) + 1;
      push(&m, sizeof m.c + used);
      report(take(*more ? (uint32_t)strtoul(more, NULL, 10) : INFINITE));
    } else if (!strcmp(line, "b")) {
      char *condition = strstr(more, " if ");
      if (condition) *condition = 0, condition += 4;
      const uint64_t *at = ask((Ask){.kind = ASK_PLACES}, more, strlen(more) + 1, &n);
      for (uint32_t i = 0, k; i < n / 8; i += 1) {
        for (k = 0; k < count && breakpoints[k] != at[i];) k += 1;
        if (k == count && count == 64) break;   /* no room for one more */
        printf("%s 0x%llx\n", k < count ? "no breakpoint at" : "breakpoint at", (unsigned long long)at[i]);
        if (k < count) breakpoints[k] = breakpoints[--count], memmove(conditions[k], conditions[count], sizeof *conditions);
        else snprintf(conditions[count], sizeof *conditions, "%s", condition ? condition : ""), breakpoints[count++] = at[i];
      }
      if (!n) puts("no code there");
    } else if (!strcmp(line, "bt")) frames(thread, 0, ~0u);
    else if (!strcmp(line, "f")) frame = (uint32_t)strtoul(more, NULL, 10), frames(thread, frame, 1);
    else if (!strcmp(line, "t")) {   /* the one looked at has a star */
      static uint32_t ids[QUEUE_BYTES / 4];   /* a copy: the frame asked for below takes the answer's place */
      const void *answer = ask((Ask){.kind = ASK_THREADS}, "", 0, &n);
      memcpy(ids, answer, n);
      for (uint32_t i = 0; i < n / 4; i += 1) printf("%c0x%x ", ids[i] == thread ? '*' : ' ', (unsigned)ids[i]), frames(ids[i], 0, 1);
    }
    else if (!strcmp(line, "l")) rows(ASK_LOCALS, "", false);
    else if (!strcmp(line, "r")) rows(ASK_REGISTERS, "", false);
    else if (!strcmp(line, "p") || !strcmp(line, "x")) rows(ASK_TEXTS, more, *line == 'x');
    else if (!strcmp(line, "q")) break;
    else puts("b <file:line or function> [if <expression>], c [ms], s, n, o, si, ni, bt, f <n>, t, l, r, p <expression>, x <expression>, q");
  }
  push(&(SessionCommand){.kind = SESSION_QUIT}, sizeof(SessionCommand));
  while (!take(INFINITE)->gone) {}
  pthread_join(s.thread, NULL);
  return !started;
}
