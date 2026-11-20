/* The names of subjects/zoo.c, from the frame that shows them (dbg/eval.c). The program calls `probe` with each
   variable's name, address, size and, for those whose address is not taken, value. Stopped at probe's first
   instruction these are in rcx, rdx, r8 and r9; the name looked up in the caller's frame must be a variable of
   that size at that address, or with that value. Optimised code may have no place for a variable at some
   instruction: one shown by value may be missing, and the count of those is printed; a place that is given
   must hold the value.
   usage: zoo_check <zoo.exe> <least number of variables that must be found> <and of them in a register> */
#include "../dbg/eval.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FAIL(...) return printf("zoo_check: " __VA_ARGS__), printf("\n"), 1

int main(int argc, char **argv)
{
  Engine e;
  uint32_t thread, shown = 0, in_memory = 0, in_register = 0, by_value = 0, references = 0, missing = 0;
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
    char name[64] = {0};
    if (ev.kind != TARGET_TRAP || !target_regs(e.target, ev.thread, &r)) FAIL("event %d (code %llx), not a stop at probe", ev.kind, (unsigned long long)ev.code);
    uint64_t addr = r.gp[2], size = r.gp[8], value = r.gp[9], got = 0;
    for (uint32_t i = 0; i < sizeof name - 1 && target_read(e.target, r.gp[1] + i, &name[i], 1) && name[i]; i += 1) {}
    if (!unwind_step(target_read, e.target, &engine_module(&e, r.rip)->image, &r)) FAIL("%s: no frame above probe", name);
    EvalPlace p = eval_name(&e, &r, true, name, strlen(name));
    uint64_t voff = r.rip - m->image.base;
    if (p.kind == EVAL_NONE && !addr && ++missing) continue;
    if (p.kind == EVAL_NONE) FAIL("%s (shown at +0x%llx): not found, or without a place", name, (unsigned long long)voff);
    /* clang gives a parameter that is passed as a pointer to a copy the type "reference to" what it is: the variable is what the reference names */
    const RdiType *type = rdi_at(&p.module->rdi, RDI_TYPE_NODES, RdiType, p.type);
    if (type->kind == 0x1002 && p.kind == EVAL_MEMORY && target_read(e.target, p.at, &p.at, 8)) p.type = type->a, references += 1;
    if (rdi_at(&p.module->rdi, RDI_TYPE_NODES, RdiType, p.type)->byte_size != size) FAIL("%s (at +0x%llx): its type has %u bytes, the program says %llu", name, (unsigned long long)voff, rdi_at(&p.module->rdi, RDI_TYPE_NODES, RdiType, p.type)->byte_size, (unsigned long long)size);
    if (addr && (p.kind != EVAL_MEMORY || p.at != addr)) FAIL("%s (at +0x%llx): place %d at %llx, the program says memory at %llx", name, (unsigned long long)voff, p.kind, (unsigned long long)p.at, (unsigned long long)addr);
    if (size < 8) value &= ((uint64_t)1 << 8 * size) - 1;
    if (!addr && (size > 8 || !eval_read(&e, &r, p, &got, (uint32_t)size) || got != value)) FAIL("%s (at +0x%llx): %llx read from place %d at %llx, the program says %llx", name, (unsigned long long)voff, (unsigned long long)got, p.kind, (unsigned long long)p.at, (unsigned long long)value);
    shown += 1, by_value += !addr, in_memory += p.kind == EVAL_MEMORY, in_register += p.kind == EVAL_REGISTER;
  }
  engine_close(&e);
  if (argc < 4 || shown < (uint32_t)atoi(argv[2]) || !by_value || in_register < (uint32_t)atoi(argv[3])) FAIL("only %u variables found, %u by value, %u in a register", shown, by_value, in_register);
  printf("zoo_check: %u variables found where the program says they are (%u by value, %u through a reference; %u in memory, %u in a register); %u shown by value have no place there\n", shown, by_value, references, in_memory, in_register, missing);
  return 0;
}
