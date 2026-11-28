/* The unwinder against the system's own (dbghelp's StackWalk64) on a live program: the program
   is single-stepped `steps` instructions from its very first, in the loader, as many from its
   entry point, in the C runtime's start, and as many from each offset given (main, say), and after
   every instruction the chain of return addresses and stack pointers from
   dbg/unwind.c must be the system's, frame for frame, as far as the system's goes.
   usage: stress_unwind <program.exe> <steps> [offset in the program, hexadecimal]... */
#include "../dbg/le.h"
#include "../dbg/unwind.h"
#include <windows.h>
#include <dbghelp.h>
#include <stdio.h>
#include <stdlib.h>

enum { FRAMES = 64, MODULES = 64 };
static UnwindModule modules[MODULES];
static uint32_t module_count;

int main(int argc, char **argv)
{
  if (argc < 3) return 2;
  /* started here, suspended, and then attached to: dbghelp needs a handle of the process, which the target keeps to itself */
  STARTUPINFOA start = {.cb = sizeof start};
  PROCESS_INFORMATION made;
  TargetEvent e;
  if (!CreateProcessA(NULL, argv[1], NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, NULL, &start, &made)) return printf("stress_unwind: cannot start %s\n", argv[1]), 1;
  Target *t = target_open(NULL, made.dwProcessId, &e);
  if (!t || !SymInitialize(made.hProcess, NULL, FALSE)) return printf("stress_unwind: cannot open the target\n"), 1;
  ResumeThread(made.hThread);
  uint64_t from;
  uint8_t rva[4];
  uint32_t steps = (uint32_t)atoi(argv[2]), done = 0, frames = 0, deepest = 0, beyond = 0, waits = 0, proven = 0, thread = made.dwThreadId;
  for (bool stopped = true, let_go = false, trapping = false; done < (uint32_t)argc * steps - steps;) {
    if (e.kind == TARGET_MODULE && module_count < MODULES && unwind_module(target_read, t, e.addr, &modules[module_count])) {
      /* (a module that cannot be read was mapped and unmapped at once, while the program ran on) */
      SymLoadModuleEx(made.hProcess, NULL, e.text, NULL, e.addr, (DWORD)modules[module_count].size, NULL, 0);   /* it refuses modules without code */
      module_count += 1;
    }
    for (uint32_t i = 0; e.kind == TARGET_MODULE_GONE && i < module_count; i += 1)
      if (modules[i].base == e.addr) free(modules[i].table), modules[i] = modules[--module_count], SymUnloadModule64(made.hProcess, e.addr);
    if (e.kind == TARGET_EXIT) break;   /* let go for a step that waited on another thread, it can run to its end: what was compared stands */
    if (stopped && e.kind != TARGET_MODULE && e.kind != TARGET_STEP && e.kind != TARGET_BREAK && e.kind != TARGET_TRAP) return printf("stress_unwind: step %u: event %d\n", done, e.kind), 1;
    if (stopped && e.thread == thread) {
      TargetRegs r;
      CONTEXT c = {.ContextFlags = CONTEXT_FULL};
      STACKFRAME64 f = {0};
      if (!target_regs(t, thread, &r) || !GetThreadContext(made.hThread, &c)) return printf("stress_unwind: step %u: no registers\n", done), 1;
      uint64_t at = r.rip;
      f.AddrPC.Offset = c.Rip, f.AddrStack.Offset = c.Rsp, f.AddrFrame.Offset = c.Rbp, f.AddrPC.Mode = f.AddrStack.Mode = f.AddrFrame.Mode = AddrModeFlat;
      for (uint32_t depth = 0; depth < FRAMES; depth += 1) {
        bool theirs = StackWalk64(IMAGE_FILE_MACHINE_AMD64, made.hProcess, made.hThread, &f, &c, NULL, SymFunctionTableAccess64, SymGetModuleBase64, NULL) && f.AddrPC.Offset;
        if (depth == 0) continue;   /* the first is the thread itself; from the second on, the callers */
        const UnwindModule *m = 0;
        for (uint32_t i = 0; i < module_count; i += 1) if (r.rip - modules[i].base < modules[i].size) m = &modules[i];
        bool ours = unwind_step(target_read, t, m, &r) && r.rip;
        if (!theirs) { beyond += ours; break; }   /* where the system's walk ends there is nothing to compare; ours may go on into what is no stack */
        /* The system's walker can be the one that is wrong (Wine's, at a tail jump after an epilogue's pops). Ours is
           right beyond doubt when it returns to just after a direct call of the very function the thread is in. */
        uint8_t call[5];
        RUNTIME_FUNCTION *fn = SymFunctionTableAccess64(made.hProcess, at);
        if (depth == 1 && ours && r.rip != f.AddrPC.Offset && fn && target_read(t, r.rip - 5, call, 5) && call[0] == 0xe8
            && r.rip + (uint64_t)(int32_t)le32(call + 1) == SymGetModuleBase64(made.hProcess, at) + fn->BeginAddress) { proven += 1; break; }
        if (!ours || r.rip != f.AddrPC.Offset || r.gp[4] != f.AddrStack.Offset)
          return printf("stress_unwind: step %u at %llx, frame %u: ours %d %llx sp %llx, the system's %d %llx sp %llx\n", done, (unsigned long long)at, depth, ours,
                        (unsigned long long)r.rip, (unsigned long long)r.gp[4], theirs, (unsigned long long)f.AddrPC.Offset, (unsigned long long)f.AddrStack.Offset), 1;
        frames += 1, deepest = depth > deepest ? depth : deepest;
      }
      done += 1;
    }
    if (stopped && done && done % steps == 0 && e.kind != TARGET_TRAP) {   /* on to the next place: the entry point (its offset is in the program's header), then the offsets given */
      if (done == steps && !(target_read(t, modules[0].base + 0x3c, rva, 4) && target_read(t, modules[0].base + le32(rva) + 24 + 16, rva, 4))) return printf("stress_unwind: no header\n"), 1;
      from = modules[0].base + (done == steps ? le32(rva) : strtoull(argv[1 + done / steps], NULL, 16));
      target_resume(t, &from, 1, 0), trapping = true;
    } else if (stopped) target_resume(t, NULL, 0, thread), trapping = false;
    stopped = target_wait(t, let_go || trapping ? 10000 : 2000, &e);
    if (!stopped && e.kind == TARGET_RUNNING) {
      if (let_go || trapping) return printf("stress_unwind: step %u: nothing for 10 s\n", done), 1;
      /* the instruction waits for a thread that the step holds still (a system call): all run until it has returned */
      target_halt(t), target_resume(t, NULL, 0, 0), let_go = true, waits += 1;
    } else if (stopped || (let_go && e.kind == TARGET_LATE && e.thread == thread && (target_halt(t), e.kind = TARGET_STEP, stopped = true))) let_go = false;   /* returned, one instruction on */
  }
  printf("stress_unwind: %u steps: %u frames agree, deepest stack %u, %u modules, %u times ours went on where the system's ended, %u steps waited for other threads, %u times the system's walk was wrong and ours proven\n", done, frames, deepest, module_count, beyond, waits, proven);
  TerminateProcess(made.hProcess, 0);   /* attached to, it would run on when the target is closed */
  target_close(t);
  return 0;
}
