/* Process control under stress (dbg/target.h, specs/Run.tla): random runs of tests/spin.exe with
   random traps, single steps (some halted at once), halts after random waits. After every stop: the program's own
   bytes are back (Clean), every module came with its path, a trap stop stands on its trap with the function's argument in rcx
   (Reported), no thread stands behind a trap byte (Settled), nothing moves, a step moved no other thread (not even one that began during it), and no exception
   but the one the program raises and handles itself, no exit and no second breakpoint of the
   program's own ever comes (a thread that went on from behind a trap byte would run a torn
   instruction: Whole). At the end every thread has got somewhere, every kind of stop has
   happened, the program has handled each of its exceptions, and, told to quit by a write to
   its memory, it ends with its exit code.
   usage: stress_target <spin.exe> <rounds> <seed> [attach] */
#include "../dbg/target.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t seed;
static uint32_t rnd(uint32_t n) { seed = seed * 1664525u + 1013904223u; return (seed >> 8) % n; }

#define FAIL(...) do { printf("stress_target: round %u: ", round), printf(__VA_ARGS__), printf("\n"); return 1; } while (0)

int main(int argc, char **argv)
{
  if (argc < 4) return 2;
  uint32_t rounds = (uint32_t)atoi(argv[2]), round = 0, seen[TARGET_EXIT + 1] = {0}, halts = 0, late_behind = 0, in_step = 0;
  uint32_t live[64], lives = 0;
  seed = (uint32_t)atoi(argv[3]);
  Target *t;
  TargetEvent image = {0};
  char mz[2];
  if (argc > 4) {   /* attach: start it on its own and let it get going */
    STARTUPINFOA start = {.cb = sizeof start};
    PROCESS_INFORMATION made;
    if (!CreateProcessA(NULL, argv[1], NULL, NULL, FALSE, 0, NULL, NULL, &start, &made)) FAIL("cannot start %s", argv[1]);
    Sleep(500);
    t = target_open(NULL, made.dwProcessId, &image);
  } else t = target_open(argv[1], 0, &image);
  if (!t) FAIL("cannot open the target");
  if (image.kind != TARGET_MODULE || !strstr(image.text, "spin.exe") || !target_read(t, image.addr, mz, 2) || memcmp(mz, "MZ", 2)) FAIL("the program is not where the first event says");
  unsigned long long fn[3] = {0}, counts = 0, quit = 0, handled = 0;
  uint32_t raised = 0;
  uint64_t traps[4];
  uint8_t own[3], now[3];
  long long before[5], after[5], first[5], base = 0;
  TargetEvent e = {.kind = TARGET_RUNNING};
  int mover = 0;
  bool known = false;   /* the program has said where its functions are, and a stop without traps has shown their bytes */
  for (; round < rounds; round += 1) {
    /* a run: random traps, one of them sometimes twice; or one instruction of the thread that stopped last, now and then halted at once */
    uint32_t trap_count = 0, step = (e.kind == TARGET_TRAP || e.kind == TARGET_STEP) && rnd(3) == 0 ? e.thread : 0;
    bool rush = step && rnd(8) == 0;
    for (int i = 0; i < 3; i += 1) if (known && !step && rnd(3) == 0) traps[trap_count++] = fn[i];
    if (trap_count && rnd(8) == 0) traps[trap_count] = traps[0], trap_count += 1;
    target_resume(t, traps, trap_count, step);
    bool stopped = false;
    for (DWORD start = GetTickCount(), halt_after = rnd(4); !stopped;) {
      stopped = target_wait(t, rush ? 0 : rnd(3), &e);
      seen[e.kind] += 1, late_behind += e.kind == TARGET_LATE && e.code == 1;
      if (e.kind == TARGET_MODULE && !strstr(e.text, ".dll") && !strstr(e.text, ".DLL")) FAIL("a module without its path: '%s'", e.text);
      if (e.kind == TARGET_THREAD && lives < 64) live[lives++] = e.thread;
      if (e.kind == TARGET_THREAD && step) in_step += 1, Sleep(2);   /* time to run, were it not held */
      for (uint32_t i = 0; e.kind == TARGET_THREAD_GONE && i < lives; i += 1) if (live[i] == e.thread) live[i--] = live[--lives];
      if (e.kind == TARGET_OUTPUT && !fn[0] && (e.code < 5 || sscanf(e.text, "spin %llx %llx %llx %llx %llx %llx", &fn[0], &fn[1], &fn[2], &counts, &quit, &handled) != 6)) FAIL("unexpected debug text");
      if (stopped || (step && !rush) || (!rush && GetTickCount() - start < halt_after)) {
        if (GetTickCount() - start > 20000) FAIL("no stop in 20 s (step of thread %u)", step);
        continue;
      }
      target_halt(t);
      halts += 1, e.kind = TARGET_RUNNING;
      break;
    }
    raised += e.kind == TARGET_EXCEPTION && e.code == 0xe0000001;
    if ((e.kind == TARGET_EXCEPTION && e.code != 0xe0000001) || e.kind == TARGET_EXIT || (e.kind == TARGET_BREAK && seen[TARGET_BREAK] > 1)) FAIL("event %d code %llx at %llx", e.kind, (unsigned long long)e.code, (unsigned long long)e.addr);
    TargetRegs regs;
    if (e.kind == TARGET_TRAP) {
      bool mine = false;
      for (uint32_t i = 0; i < trap_count; i += 1) mine = mine || traps[i] == e.addr;
      if (!mine || !target_regs(t, e.thread, &regs) || regs.rip != e.addr || regs.gp[1] > 4 || regs.gp[4] % 8 != 0 || regs.gp[4] == 0)
        FAIL("a trap stop at %llx is not on a trap of this run with its argument in rcx", (unsigned long long)e.addr);
      mover = (int)regs.gp[1];   /* the counter of the thread that steps from here */
    }
    if (e.kind == TARGET_STEP && e.thread != step) FAIL("a step of thread %u was not asked for", e.thread);
    for (uint32_t i = 0; i < lives; i += 1)   /* Settled: no thread stands behind a trap byte (spin's three functions begin with longer instructions, so none can stand there otherwise) */
      for (uint32_t k = 0; k < trap_count; k += 1) {
        if (!target_regs(t, live[i], &regs)) break;   /* it has ended and the event has not come yet */
        if (regs.rip == traps[k] + 1) FAIL("thread %u stands behind a trap byte while stopped", live[i]);
      }
    if (!fn[0]) continue;
    if (!target_read(t, fn[0], now, 1) || !target_read(t, fn[1], now + 1, 1) || !target_read(t, fn[2], now + 2, 1) || !target_read(t, counts, before, sizeof before)) FAIL("cannot read the target");
    if (!known) {   /* this run had no traps; attached, the program has handled exceptions before we came */
      if (!target_read(t, handled, &base, sizeof base)) FAIL("cannot read the target");
      memcpy(own, now, 3), memcpy(first, before, sizeof first), known = true, raised = e.kind == TARGET_EXCEPTION;
    }
    if (memcmp(own, now, 3) != 0) FAIL("a trap byte is in memory while stopped");
    /* a step ran its thread alone: since the stop before, one instruction can have added to that thread's counter */
    for (int i = 0; step && i < 5; i += 1)
      if (before[i] != after[i] && (i != mover || before[i] - after[i] > 3)) FAIL("other threads ran during a step: counter %d went from %lld to %lld", i, after[i], before[i]);
    if (round % 16 == 0) Sleep(2);
    if (!target_read(t, counts, after, sizeof after) || memcmp(before, after, sizeof after) != 0) FAIL("the target moves while stopped");
  }
  for (int i = 0; i < 5; i += 1) if (!known || after[i] <= first[i]) FAIL("thread %d of the target got nowhere", i);
  if (!seen[TARGET_TRAP] || !seen[TARGET_STEP] || !halts || !seen[TARGET_MODULE] || !seen[TARGET_THREAD] || !seen[TARGET_THREAD_GONE] || !seen[TARGET_OUTPUT] || !raised) FAIL("a kind of event never came");
  /* An exception the target stands on is still to be handled, and so is one whose thread has not run since it was
     reported (a stop came first, then steps of another thread): the program runs on until none is on its way.
     One that the program was never given stays missing. Then tell it to quit. */
  long long did = 0;
  for (int more = 0; more < 200; more += 1) {
    if (!target_read(t, handled, &did, sizeof did)) FAIL("cannot read the target");
    if (did - base == raised - (e.kind == TARGET_EXCEPTION)) break;
    target_resume(t, NULL, 0, 0);
    if (target_wait(t, 5, &e)) raised += e.kind == TARGET_EXCEPTION; else target_halt(t), e.kind = TARGET_RUNNING;
  }
  if (did - base != raised - (e.kind == TARGET_EXCEPTION)) FAIL("the program handled %lld of its %u exceptions", did - base, raised);
  if (!target_write(t, quit, &(int){1}, sizeof(int))) FAIL("cannot write the target");
  for (DWORD start = GetTickCount(); e.kind != TARGET_EXIT;) {
    target_resume(t, NULL, 0, 0);
    while (!target_wait(t, 100, &e)) if (GetTickCount() - start > 20000) FAIL("it does not quit");
    if (e.kind != TARGET_EXIT && !(e.kind == TARGET_EXCEPTION && e.code == 0xe0000001)) FAIL("event %d while it quits", e.kind);
  }
  if (e.code != 7) FAIL("exit code %llu", (unsigned long long)e.code);
  target_close(t);
  printf("stress_target: %u rounds: %u trap stops, %u steps, %u halts, %u late events (%u with the thread still behind the trap byte), %u threads begun (%u during a step), %u debug strings, %u exceptions handled by the program\n",
         rounds, seen[TARGET_TRAP], seen[TARGET_STEP], halts, seen[TARGET_LATE], late_behind, seen[TARGET_THREAD], in_step, seen[TARGET_OUTPUT], raised);
  return 0;
}
