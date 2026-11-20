/* The engine where RAD's recorded traces do not reach (tests/oracle_trace.py): several threads in the same code,
   breakpoints, and the exceptions that are no stop. On tests/scene.exe:
   - a breakpoint in a function three threads run: every stop is on it, and between two stops of one thread that
     thread has run the function exactly once (it was stepped off the breakpoint, alone, and ran no torn instruction);
   - one thread steps over instructions, calls among them, while the others run through the step's traps: every
     step ends with that thread just behind the instruction, at the same depth;
   - an exception the program handles is no stop; one nobody handles is, once; a fault is at once, and again
     at every run, for the program is not given it.
   usage: stress_engine <scene.exe> <rounds> <caller> <shared> <handled> <go>   (addresses as the linker gave them) */
#include "../dbg/engine.h"
#include "../dbg/x64.h"
#include <stdio.h>
#include <stdlib.h>

#define FAIL(...) return printf("stress_engine: " __VA_ARGS__), printf("\n"), 1

int main(int argc, char **argv)
{
  Engine e;
  uint32_t first;
  if (argc < 7 || !engine_open(&e, argv[1], 0, &first)) FAIL("cannot start");
  uint64_t at[4];   /* caller, shared, handled, go: the linker counted from the base it asked for */
  for (int i = 0; i < 4; i += 1) at[i] = strtoull(argv[3 + i], 0, 16) - 0x140000000 + e.modules[0].image.base;
  uint32_t rounds = (uint32_t)atoi(argv[2]), threads[3] = {0}, stepped = 0, calls = 0;
  int32_t count[3] = {0}, now;
  TargetRegs r;
  TargetEvent ev = {0};
  for (uint32_t round = 0; round < rounds; round += 1) {
    ev = engine_run(&e, ENGINE_RUN, 0, &at[1], 1);
    if (ev.kind == TARGET_BREAK && !round) ev = engine_run(&e, ENGINE_RUN, 0, &at[1], 1);   /* the loader's own */
    if (ev.kind != TARGET_TRAP || ev.addr != at[1] || !target_regs(e.target, ev.thread, &r) || r.rip != at[1] || !target_read(e.target, r.gp[1], &now, 4))
      FAIL("round %u: event %d at %llx, not a stop on the breakpoint", round, ev.kind, (unsigned long long)ev.addr);
    uint32_t t = 0;
    while (t < 3 && threads[t] && threads[t] != ev.thread) t += 1;
    if (t == 3) FAIL("round %u: a fourth thread on the breakpoint", round);
    if (threads[t] && now != count[t] + 1) FAIL("round %u: between two stops thread %u ran the function %d times", round, ev.thread, now - count[t]);
    threads[t] = ev.thread, count[t] = now;
  }
  /* the thread that stopped last, instruction by instruction: out of shared, and round and round in caller and worker */
  for (uint32_t round = 0; round < rounds; round += 1) {
    uint8_t code[15];
    if (!target_regs(e.target, ev.thread, &r) || !target_read(e.target, r.rip, code, 15)) FAIL("step %u: cannot read the thread", round);
    X64Inst x = x64_decode(code, 15, r.rip);
    uint64_t depth = r.gp[4], next = r.rip + x.size;
    uint32_t thread = ev.thread;
    ev = engine_run(&e, ENGINE_STEP_OVER_INST, thread, NULL, 0);
    if (ev.kind != TARGET_STEP || ev.thread != thread || !target_regs(e.target, thread, &r) || r.rip != ev.addr) FAIL("step %u: event %d, thread %u at %llx", round, ev.kind, ev.thread, (unsigned long long)ev.addr);
    if (x.flow == X64_CALL && (r.rip != next || r.gp[4] != depth)) FAIL("step %u: a call was not stepped over", round);
    stepped += 1, calls += x.flow == X64_CALL;
  }
  if (rounds && (!threads[2] || !calls)) FAIL("not every thread came to the breakpoint, or no call was stepped over");
  /* the exceptions */
  if (!target_write(e.target, at[3], &(int32_t){1}, 4)) FAIL("cannot write the target");
  ev = engine_run(&e, ENGINE_RUN, 0, NULL, 0);
  if (ev.kind != TARGET_EXCEPTION || ev.code != 0xe0000002 || !ev.unhandled || !target_read(e.target, at[2], &now, 4) || now != 1)
    FAIL("event %d code %llx, not the exception nobody handles after the one the program handled", ev.kind, (unsigned long long)ev.code);
  for (int i = 0; i < 2; i += 1) {
    ev = engine_run(&e, ENGINE_RUN, 0, NULL, 0);
    if (ev.kind != TARGET_EXCEPTION || ev.code != 0xc0000005 || ev.unhandled) FAIL("event %d code %llx, not the fault, at once (run %d)", ev.kind, (unsigned long long)ev.code, i);
  }
  engine_close(&e);
  printf("stress_engine: %u stops on a breakpoint in three threads, %u instructions stepped over (%u calls), exceptions as they should be\n", rounds, stepped, calls);
  return 0;
}
