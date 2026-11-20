/* Steps a program as one of RAD's recorded traces did (oracle/rad-trace.ps1, traces/), and prints
   where each step ends in the trace's own form, for tests/oracle_trace.py to compare:
     <command> -> <function> <file>:<line> +0x<offset> [<why it stopped>]
     <command> -> outside <module>!<function>      (the trace ends when the thread leaves the program)
     <command> -> ended
   usage: trace <program.exe> <into|over|mix|mix2|back> <most steps>   (the recorder's scripts) */
#include "../dbg/engine.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *leaf(const char *path, uint64_t n)   /* what follows the last slash */
{
  const char *at = path + n;
  while (at > path && at[-1] != '/' && at[-1] != '\\') at -= 1;
  return at;
}

static bool in_main;   /* the place last printed */

/* Prints where rip is; false when the trace ends here. */
static bool place(const Engine *e, uint64_t rip, const char *why)
{
  const EngineModule *m = engine_module(e, rip);
  static const Rdi none;
  const Rdi *r = m ? &m->rdi : &none;
  uint64_t off = m ? rip - m->image.base : 0;
  RdiSpan name = rdi_str(r, rdi_at(r, RDI_PROCEDURES, RdiSymbol, rdi_at(r, RDI_SCOPES, RdiScope, rdi_vmap(r, RDI_SCOPE_VMAP, off))->proc)->name);
  in_main = name.n == 4 && !memcmp(name.p, "main", 4);
  if (m != e->modules) return printf("outside %s!%.*s\n", m ? leaf(m->path, strlen(m->path)) : "", (int)name.n, (const char *)name.p), false;
  RdiRow row = rdi_row(r, off);
  RdiLine line = row.line;
  char path[1024], number[12] = "";   /* no number where no line table has the place (RAD's is empty there); 0 where one has it without a line */
  if (row.opl) snprintf(number, sizeof number, "%u", line.line);
  uint64_t n = rdi_path(r, rdi_at(r, RDI_SOURCE_FILES, RdiSourceFile, line.file)->path_node, path, sizeof path);
  if (n > sizeof path) n = sizeof path;
  const char *file = leaf(path, n);
  printf("%.*s %.*s:%s +0x%llx [%s]\n", (int)name.n, (const char *)name.p, (int)(path + n - file), file, number, (unsigned long long)off, why);
  return true;
}

/* How deep the thread's frame is: the stack pointer its caller gets back (0: not known). RAD's traces name places, not depths, and a step over
   a call of the function to itself that ends in the inner call ends at the right place: so the replay says itself when a step over or out ends deeper than it began. */
static uint64_t depth(const Engine *e, uint32_t thread)
{
  TargetRegs r;
  const EngineModule *m;
  return target_regs(e->target, thread, &r) && (m = engine_module(e, r.rip)) && unwind_step(target_read, e->target, &m->image, &r) ? r.gp[4] : 0;
}

int main(int argc, char **argv)
{
  static const char *const names[] = {[ENGINE_STEP_INTO_INST] = "step_into_inst", "step_over_inst", "step_into_line", "step_over_line", "step_out"};
  static const int mix[7] = {ENGINE_STEP_INTO_LINE, ENGINE_STEP_INTO_LINE, ENGINE_STEP_OVER_LINE, ENGINE_STEP_OVER_LINE, ENGINE_STEP_OUT, ENGINE_STEP_OVER_INST, ENGINE_STEP_INTO_INST};
  Engine e;
  uint32_t thread;
  setvbuf(stdout, NULL, _IONBF, 0);   /* a replay that hangs has shown how far it came */
  if (argc < 4 || !engine_open(&e, argv[1], 0, &thread)) return 2;
  uint64_t entry = engine_entry(&e);
  double seed = strcmp(argv[2], "mix2") ? 12345 : 54321;   /* the recorder's generator, in the arithmetic of the recorder's language: numbers this large it rounds */
  TargetEvent ev;
  int deep = 0;
  do ev = engine_run(&e, ENGINE_RUN, 0, &entry, 1); while (ev.kind == TARGET_BREAK);   /* the loader's own breakpoint: RAD goes on from it without a word */
  printf("start -> ");
  bool on = ev.kind == TARGET_TRAP && place(&e, ev.addr, "stopped at entry point");
  for (int n = 0, cap = atoi(argv[3]); on && n < cap; n += 1) {
    int how = !strcmp(argv[2], "into") ? ENGINE_STEP_INTO_LINE : !strcmp(argv[2], "over") ? ENGINE_STEP_OVER_LINE
            : !strcmp(argv[2], "back") ? (in_main || ++deep % 2 ? ENGINE_STEP_INTO_LINE : ENGINE_STEP_OUT) : mix[(uint64_t)((seed = fmod(seed * 1103515245.0 + 12345.0, 2147483648.0)) / 65536) % 7];
    if (how == ENGINE_STEP_OUT && in_main && !strcmp(argv[2], "mix2")) how = ENGINE_STEP_INTO_LINE;
    uint64_t before = how == ENGINE_STEP_INTO_LINE || how == ENGINE_STEP_INTO_INST ? 0 : depth(&e, ev.thread);
    ev = engine_run(&e, how, ev.thread, NULL, 0);   /* the thread that stopped last is the one a command is for */
    printf("%s -> ", names[how]);
    if (ev.kind == TARGET_EXIT) on = false, printf("ended\n");
    else on = place(&e, ev.addr, ev.kind == TARGET_STEP ? depth(&e, ev.thread) < before ? "completed step deeper than it began" : "completed step" : ev.kind == TARGET_EXCEPTION ? "hit an exception" : "?");
  }
  fflush(stdout);
  engine_close(&e);
  return 0;
}
