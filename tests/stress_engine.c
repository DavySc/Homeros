/* The engine where RAD's recorded traces do not reach (tests/oracle_trace.py): several threads in the same code,
   breakpoints, and the exceptions that are no stop. On tests/scene.exe:
   - a breakpoint in a function three threads run: every stop is on it, and between two stops of one thread that
     thread has run the function exactly once (it was stepped off the breakpoint, alone, and ran no torn instruction);
   - one thread steps over instructions, calls and a repeated instruction among them, while the others run through
     the step's traps: every step ends with that thread just behind the instruction, at the same depth;
   - a halt, asked for after 0 to 3 waits, ends a run that has no breakpoint: no thread moves any more, and the
     next run comes to the breakpoint as before;
   - an exception the program handles is no stop; one nobody handles is, once; a fault is at once, and again
     at every run, for the program is not given it;
   - a thread inside a fault, with a trap on the byte before it, has not run that trap: a halt leaves it where it is.
   usage: stress_engine <scene.exe> <rounds> <caller> <shared> <handled> <go>   (addresses as the linker gave them) */
#include "../dbg/engine.h"
#include "../dbg/x64.h"
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

static bool soon(void *waits) { return (*(uint32_t *)waits)-- == 0; }

#define FAIL(...) return printf("stress_engine: " __VA_ARGS__), printf("\n"), 1

int main(int argc, char **argv)
{
  Engine e;
  uint32_t first;
  if (argc < 7 || !engine_open(&e, argv[1], 0, &first)) FAIL("cannot start");
  uint64_t at[4];   /* caller, shared, handled, go: the linker counted from the base it asked for */
  for (int i = 0; i < 4; i += 1) at[i] = strtoull(argv[3 + i], 0, 16) - 0x140000000 + e.modules[0].image.base;
  uint32_t rounds = (uint32_t)atoi(argv[2]), threads[3] = {0}, stepped = 0, calls = 0, repeats = 0, halts = 0, waits;
  int32_t count[3] = {0}, now;
  TargetRegs r;
  TargetEvent ev = {0};
  for (uint32_t round = 0; round < rounds; round += 1) {
    ev = engine_run(&e, ENGINE_RUN, 0, &at[1], 1);
    if (ev.kind == TARGET_BREAK && !round) ev = engine_run(&e, ENGINE_RUN, 0, &at[1], 1);   /* the loader's own */
    if (ev.kind != TARGET_TRAP || ev.addr != at[1] || !target_regs(e.target, ev.thread, &r) || r.rip != at[1] || !target_read(e.target, r.gp[1], &now, 4)) {
      for (int i = 0; i < 3; i += 1) if (target_regs(e.target, threads[i], &r)) printf("stress_engine: thread %u (function run %d times) stands at the breakpoint + %lld\n", threads[i], count[i], (long long)(r.rip - at[1]));
      target_regs(e.target, ev.thread, &r);
      FAIL("round %u: event %d (code %llx%s) of thread %u at the breakpoint + %lld, standing at + %lld: not a stop on the breakpoint", round, ev.kind, (unsigned long long)ev.code,
           ev.unhandled ? ", unhandled" : "", ev.thread, (long long)(ev.addr - at[1]), (long long)(r.rip - at[1]));
    }
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
    if (x.repeats && r.rip != next) FAIL("step %u: a repeated instruction was stepped into", round);
    stepped += 1, calls += x.flow == X64_CALL, repeats += x.repeats;
  }
  if (rounds && (!threads[2] || !calls || !repeats)) FAIL("not every thread came to the breakpoint (the third: %u), or no call (%u) or no repeated instruction (%u) was stepped over", threads[2], calls, repeats);
  for (e.halt = soon, e.halt_arg = &waits; halts < rounds / 20; halts += 1) {
    uint64_t rip[3];
    waits = halts % 4;
    ev = engine_run(&e, ENGINE_RUN, 0, NULL, 0);
    for (int i = 0; i < 3; i += 1) if (ev.kind != TARGET_HALT || !target_regs(e.target, threads[i], &r) || (rip[i] = r.rip) == 0) FAIL("halt %u: event %d, not a halt with every thread stopped", halts, ev.kind);
    Sleep(2);
    for (int i = 0; i < 3; i += 1) if (!target_regs(e.target, threads[i], &r) || r.rip != rip[i]) FAIL("halt %u: thread %u moves after the halt", halts, threads[i]);
    waits = ~0u;
    ev = engine_run(&e, ENGINE_RUN, 0, &at[1], 1);
    if (ev.kind != TARGET_TRAP || ev.addr != at[1]) FAIL("halt %u: event %d, not the breakpoint after a halt", halts, ev.kind);
  }
  e.halt = 0;
  /* the exceptions */
  if (!target_write(e.target, at[3], &(int32_t){1}, 4)) FAIL("cannot write the target");
  ev = engine_run(&e, ENGINE_RUN, 0, NULL, 0);
  if (ev.kind != TARGET_EXCEPTION || ev.code != 0xe0000002 || !ev.unhandled || !target_read(e.target, at[2], &now, 4) || now != 1)
    FAIL("event %d code %llx, not the exception nobody handles after the one the program handled", ev.kind, (unsigned long long)ev.code);
  for (int i = 0; i < 2; i += 1) {
    ev = engine_run(&e, ENGINE_RUN, 0, NULL, 0);
    if (ev.kind != TARGET_EXCEPTION || ev.code != 0xc0000005 || ev.unhandled) FAIL("event %d code %llx, not the fault, at once (run %d)", ev.kind, (unsigned long long)ev.code, i);
  }
  /* the fault's event is answered and comes again, and waits unread while the halt stops the target */
  uint64_t before = ev.addr - 1;
  target_swallow(e.target), target_resume(e.target, &before, 1, 0);
  Sleep(50);
  target_halt(e.target);
  if (!target_regs(e.target, ev.thread, &r) || r.rip != ev.addr) FAIL("a halt moved the thread that is inside a fault by %lld", (long long)(r.rip - ev.addr));
  engine_close(&e);
  printf("stress_engine: %u stops on a breakpoint in three threads, %u instructions stepped over (%u calls, %u repeated), %u halts, exceptions as they should be\n", rounds, stepped, calls, repeats, halts);
  return 0;
}
