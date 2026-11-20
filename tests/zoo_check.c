/* The names and expressions of subjects/zoo.c, worked out at the frame that shows them (dbg/eval.c). The program
   calls `probe` with a text, an address, a size and a value. Stopped at probe's first instruction these are in
   rcx, rdx, r8 and r9; the text evaluated in the caller's frame must be of that size and, when an address above 1
   is shown, in memory there; else its value as an unsigned long long (address 0) or as a double (address 1) must be
   that value (a bit field is shown with size 0, which is not compared). Optimised code may have no place for a variable at some instruction (clang's has
   none for many whose address is taken): the count of texts with such a variable is printed; a place that is given
   must hold the value.
   usage: zoo_check <zoo.exe> <number of texts the program shows> <least that must be right> <and of them variables in a register> */
#include "../dbg/eval.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FAIL(...) return printf("zoo_check: " __VA_ARGS__), printf("\n"), 1

int main(int argc, char **argv)
{
  Engine e;
  uint32_t thread, shown = 0, in_memory = 0, in_register = 0, by_value = 0, missing = 0;
  if (argc < 2 || !engine_open(&e, argv[1], 0, &thread)) FAIL("cannot start");
  const EngineModule *m = e.modules;
  RdiSpan found = rdi_matches(&m->rdi, RDI_NAMES_PROCEDURES, "probe", 5);
  if (!found.n) FAIL("no probe in the debug information");
  uint64_t probe = m->image.base + *rdi_at(&m->rdi, RDI_SCOPE_VOFFS, uint64_t, rdi_at(&m->rdi, RDI_SCOPES, RdiScope, rdi_at(&m->rdi, RDI_PROCEDURES, RdiSymbol, *(const uint32_t *)found.p)->root_scope)->voff_first);
  for (;;) {
    TargetEvent ev = engine_run(&e, ENGINE_RUN, 0, &probe, 1);
    if (ev.kind == TARGET_BREAK && !shown) continue;   /* the loader's own */
    if (ev.kind == TARGET_EXIT) break;
    TargetRegs r;
    char text[128] = {0}, as[160];
    const char *why;
    if (ev.kind != TARGET_TRAP || !target_regs(e.target, ev.thread, &r)) FAIL("event %d (code %llx), not a stop at probe", ev.kind, (unsigned long long)ev.code);
    uint64_t addr = r.gp[2], size = r.gp[8], value = r.gp[9];
    for (uint32_t i = 0; i < sizeof text - 1 && target_read(e.target, r.gp[1] + i, &text[i], 1) && text[i]; i += 1) {}
    if (!unwind_step(target_read, e.target, &engine_module(&e, r.rip)->image, &r)) FAIL("%s: no frame above probe", text);
    unsigned long long voff = r.rip - m->image.base;
    EvalValue v = eval(&e, &r, true, text, &why);
    if (!v.kind && !strcmp(why, "it has no place here") && ++missing) continue;
    if (!v.kind) FAIL("%s (shown at +0x%llx): %s", text, voff, why);
    if (size && eval_size(v.type) != size) FAIL("%s (at +0x%llx): its type has %u bytes, the program says %llu", text, voff, eval_size(v.type), (unsigned long long)size);
    if (addr > 1 && (v.kind != EVAL_MEMORY || v.at != addr)) FAIL("%s (at +0x%llx): kind %d at %llx, the program says memory at %llx", text, voff, v.kind, (unsigned long long)v.at, (unsigned long long)addr);
    snprintf(as, sizeof as, "(%s)(%s)", addr ? "double" : "unsigned long long", text);
    EvalValue is = addr > 1 ? v : eval(&e, &r, true, as, &why);
    if (addr < 2 && (is.kind != EVAL_VALUE || is.at != value)) FAIL("%s (at +0x%llx): kind %d, %llx (%s), the program says %llx", as, voff, is.kind, (unsigned long long)is.at, why ? why : "no error", (unsigned long long)value);
    shown += 1, by_value += addr < 2, in_memory += v.kind == EVAL_MEMORY, in_register += v.kind == EVAL_REGISTER;
  }
  engine_close(&e);
  if (argc < 5 || shown + missing != (uint32_t)atoi(argv[2]) || shown < (uint32_t)atoi(argv[3]) || in_register < (uint32_t)atoi(argv[4])) FAIL("%u texts shown, only %u right, %u variables in a register", shown + missing, shown, in_register);
  printf("zoo_check: %u names and expressions are what the program says (%u by value; %u in memory, %u variables in a register); %u have a variable without a place there\n", shown, by_value, in_memory, in_register, missing);
  return 0;
}
