/* Process control on Windows: the Win32 debug interface behind dbg/target.h. */
#include "../target.h"
#include <windows.h>
#include <stdlib.h>

typedef struct { DWORD id; HANDLE handle; } Thread;
typedef struct { uint64_t addr; uint8_t own; bool in; } Trap;
struct Target {
  HANDLE process;
  bool attached;                /* it ran before the debugger came, and runs on when the debugger goes */
  DWORD pid, held;              /* pid: 0 once it has ended. held: the thread whose debug event is not yet continued; 0 after a halt */
  DWORD held_status;            /* what to answer it with */
  DWORD step;                   /* the thread this run steps, or 0 */
  char *text; uint32_t text_cap;   /* the last debug string or module path */
  Thread *threads; uint32_t thread_count, thread_cap;
  Trap *traps; uint32_t trap_count, trap_cap;   /* of this run; all out of memory while stopped */
};

static HANDLE thread_handle(const Target *t, DWORD id)
{
  for (uint32_t i = 0; i < t->thread_count; i += 1) if (t->threads[i].id == id) return t->threads[i].handle;
  return NULL;
}

static void thread_add(Target *t, DWORD id, HANDLE handle)
{
  if (t->thread_count == t->thread_cap && !(t->threads = realloc(t->threads, (t->thread_cap = t->thread_cap * 2 + 8) * sizeof *t->threads))) abort();
  t->threads[t->thread_count++] = (Thread){id, handle};
}

static char *text_room(Target *t, uint32_t n)
{
  if (n > t->text_cap && !(t->text = realloc(t->text, t->text_cap = n))) abort();
  return t->text;
}

/* The path of the file a module event came with (the event's handle is ours to close), in t->text. */
static const char *module_path(Target *t, HANDLE file)
{
  char *text = text_room(t, MAX_PATH);
  DWORD n = file ? GetFinalPathNameByHandleA(file, text, t->text_cap, 0) : 0;
  if (n >= t->text_cap) text = text_room(t, n + 1), n = GetFinalPathNameByHandleA(file, text, t->text_cap, 0);   /* a longer path: the size it needs came back */
  if (file) CloseHandle(file);
  text[n < t->text_cap ? n : 0] = 0;
  return t->text;
}

/* The first event of a process is its creation; it is held, so the target is stopped. */
Target *target_open(const char *command_line, uint32_t pid, TargetEvent *first)
{
  STARTUPINFOA start = {.cb = sizeof start};
  PROCESS_INFORMATION made = {0};
  char *line = command_line ? _strdup(command_line) : NULL;   /* CreateProcess may write to it */
  BOOL ok = line ? CreateProcessA(NULL, line, NULL, NULL, FALSE, DEBUG_ONLY_THIS_PROCESS, NULL, NULL, &start, &made) : DebugActiveProcess(pid);
  free(line);
  DEBUG_EVENT e;
  if (!ok || !WaitForDebugEvent(&e, INFINITE) || e.dwDebugEventCode != CREATE_PROCESS_DEBUG_EVENT) return NULL;
  if (made.hProcess) CloseHandle(made.hProcess), CloseHandle(made.hThread);   /* the event's handles serve */
  Target *t = calloc(1, sizeof *t);
  if (!t) abort();
  *t = (Target){.attached = !command_line, .process = e.u.CreateProcessInfo.hProcess, .pid = e.dwProcessId, .held = e.dwThreadId, .held_status = DBG_CONTINUE};
  thread_add(t, e.dwThreadId, e.u.CreateProcessInfo.hThread);
  *first = (TargetEvent){.kind = TARGET_MODULE, .thread = e.dwThreadId, .addr = (uintptr_t)e.u.CreateProcessInfo.lpBaseOfImage, .text = module_path(t, e.u.CreateProcessInfo.hFile)};
  return t;
}

bool target_read(Target *t, uint64_t addr, void *bytes, uint32_t n)
{
  SIZE_T got = 0;
  return ReadProcessMemory(t->process, (LPCVOID)(uintptr_t)addr, bytes, n, &got) && got == n;
}

bool target_write(Target *t, uint64_t addr, const void *bytes, uint32_t n)
{
  SIZE_T put = 0;
  return WriteProcessMemory(t->process, (LPVOID)(uintptr_t)addr, bytes, n, &put) && put == n && FlushInstructionCache(t->process, (LPCVOID)(uintptr_t)addr, n);
}

uint32_t target_threads(const Target *t, uint32_t *ids, uint32_t room)
{
  uint32_t n = 0;
  for (; n < room && n < t->thread_count; n += 1) ids[n] = t->threads[n].id;
  return n;
}

bool target_regs(Target *t, uint32_t thread, TargetRegs *regs)
{
  CONTEXT c = {.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER};
  if (!GetThreadContext(thread_handle(t, thread), &c)) return false;
  const DWORD64 *gp = &c.Rax;   /* Rax, Rcx, Rdx, Rbx, Rsp, Rbp, Rsi, Rdi, R8 to R15 lie in a row */
  for (int i = 0; i < 16; i += 1) regs->gp[i] = gp[i];
  regs->rip = c.Rip, regs->rflags = c.EFlags;
  return true;
}

/* Sets the instruction pointer, and with `step` the flag that traps after one instruction. True when the pointer was elsewhere. */
static bool thread_at(HANDLE thread, const uint64_t *rip, bool step)
{
  CONTEXT c = {.ContextFlags = CONTEXT_CONTROL};
  if (!GetThreadContext(thread, &c)) return false;
  bool moved = rip && c.Rip != *rip;
  if (rip) c.Rip = *rip;
  if (step) c.EFlags |= 0x100;
  return SetThreadContext(thread, &c) && moved;
}

/* All own bytes are read before any trap is written, so an address named twice keeps its own byte. */
void target_resume(Target *t, const uint64_t *traps, uint32_t count, uint32_t step_thread)
{
  if (count > t->trap_cap && !(t->traps = realloc(t->traps, (t->trap_cap = count) * sizeof *t->traps))) abort();
  t->trap_count = count;
  for (uint32_t i = 0; i < count; i += 1) t->traps[i] = (Trap){.addr = traps[i]}, t->traps[i].in = target_read(t, traps[i], &t->traps[i].own, 1);
  for (uint32_t i = 0; i < count; i += 1) if (t->traps[i].in) t->traps[i].in = target_write(t, traps[i], &(uint8_t){0xcc}, 1);
  if ((t->step = step_thread)) thread_at(thread_handle(t, step_thread), NULL, true);
  for (uint32_t i = 0; step_thread && i < t->thread_count; i += 1) if (t->threads[i].id != step_thread) SuspendThread(t->threads[i].handle);   /* until the stop */
  if (t->held) ContinueDebugEvent(t->pid, t->held, t->held_status);
  else for (uint32_t i = 0; i < t->thread_count; i += 1) ResumeThread(t->threads[i].handle);
  t->held = 0;
}

/* The stop: the traps are out. A thread can have run a trap byte without its event having come yet (Windows
   hands over one event of a process at a time, specs/Run.tla): it stands one byte behind the trap until its
   event comes in a later run (TARGET_LATE, or TARGET_TRAP when the trap is planted again), which puts it back.
   Not here: asked, the system says that such a thread is inside an exception (CONTEXT_EXCEPTION_REQUEST), but
   so is a thread that ran the program's own one-byte instruction there earlier and whose next instruction
   faults, and that one, put back, runs the instruction twice (Run_flag_twice; on Windows about once in 3,000
   stops of three threads on one breakpoint). As RAD, which leaves all threads where they are. */
static void stop(Target *t)
{
  for (uint32_t i = 0; i < t->trap_count; i += 1) if (t->traps[i].in) target_write(t, t->traps[i].addr, &t->traps[i].own, 1);
  for (uint32_t i = 0; t->step && i < t->thread_count; i += 1) if (t->threads[i].id != t->step) ResumeThread(t->threads[i].handle);
}

bool target_wait(Target *t, uint32_t ms, TargetEvent *out)
{
  DEBUG_EVENT e;
  *out = (TargetEvent){.kind = TARGET_RUNNING};
  if (!WaitForDebugEvent(&e, ms)) return false;
  out->thread = e.dwThreadId;
  DWORD status = DBG_CONTINUE;
  switch (e.dwDebugEventCode) {
  case CREATE_THREAD_DEBUG_EVENT:
    thread_add(t, e.dwThreadId, e.u.CreateThread.hThread), out->kind = TARGET_THREAD;
    if (t->step) SuspendThread(e.u.CreateThread.hThread);   /* begun before the step, heard of only now: it has run nothing yet, and waits like the others */
    break;
  case EXIT_THREAD_DEBUG_EVENT:
    for (uint32_t i = 0; i < t->thread_count; i += 1) if (t->threads[i].id == e.dwThreadId) t->threads[i] = t->threads[--t->thread_count];
    out->kind = TARGET_THREAD_GONE;
    break;
  case LOAD_DLL_DEBUG_EVENT:
    out->kind = TARGET_MODULE, out->addr = (uintptr_t)e.u.LoadDll.lpBaseOfDll, out->text = module_path(t, e.u.LoadDll.hFile);
    break;
  case UNLOAD_DLL_DEBUG_EVENT: out->kind = TARGET_MODULE_GONE, out->addr = (uintptr_t)e.u.UnloadDll.lpBaseOfDll; break;
  case OUTPUT_DEBUG_STRING_EVENT:   /* readable now, while the thread that wrote it waits for the answer */
    out->code = e.u.DebugString.nDebugStringLength;
    if (target_read(t, (uintptr_t)e.u.DebugString.lpDebugStringData, text_room(t, (uint32_t)out->code), (uint32_t)out->code)) out->kind = TARGET_OUTPUT, out->text = t->text;
    break;
  case EXIT_PROCESS_DEBUG_EVENT:   /* nothing is left to hold */
    ContinueDebugEvent(e.dwProcessId, e.dwThreadId, DBG_CONTINUE);
    *out = (TargetEvent){.kind = TARGET_EXIT, .thread = e.dwThreadId, .code = e.u.ExitProcess.dwExitCode};
    t->pid = t->held = 0;
    return true;
  case EXCEPTION_DEBUG_EVENT: {
    uint64_t at = (uintptr_t)e.u.Exception.ExceptionRecord.ExceptionAddress;
    out->addr = at, out->code = e.u.Exception.ExceptionRecord.ExceptionCode, out->unhandled = !e.u.Exception.dwFirstChance;
    /* a step asked of another thread in an earlier run, done only now, is no stop of this run */
    out->kind = out->code != EXCEPTION_SINGLE_STEP ? TARGET_EXCEPTION : e.dwThreadId == t->step ? TARGET_STEP : TARGET_LATE;
    if (out->code == EXCEPTION_BREAKPOINT) {
      /* A trap of this run: the thread goes back onto it. Else the byte there says whose it was: a breakpoint
         of the program's own stays behind the thread (as RAD: win32_demon.c, "explicit trap"); anything else
         was a trap of an earlier run whose event comes only now (specs/Run.tla), and the thread goes back to
         run the instruction that is there. */
      uint8_t own = 0;
      out->kind = TARGET_LATE;
      for (uint32_t i = 0; i < t->trap_count; i += 1) if (t->traps[i].addr == at && t->traps[i].in) out->kind = TARGET_TRAP;
      if (out->kind == TARGET_LATE && target_read(t, at, &own, 1) && (own == 0xcc || own == 0xcd)) out->kind = TARGET_BREAK;
      else out->code = thread_at(thread_handle(t, e.dwThreadId), &at, false);
    } else if (out->kind == TARGET_EXCEPTION) status = DBG_EXCEPTION_NOT_HANDLED;   /* the program's handlers get it */
  } break;
  default: break;   /* RIP_EVENT, and a second process cannot come (DEBUG_ONLY_THIS_PROCESS) */
  }
  if (out->kind < TARGET_TRAP) return ContinueDebugEvent(e.dwProcessId, e.dwThreadId, status), false;
  stop(t);
  t->held = e.dwThreadId, t->held_status = status;
  return true;
}

void target_swallow(Target *t) { t->held_status = DBG_CONTINUE; }

void target_halt(Target *t)
{
  for (uint32_t i = 0; i < t->thread_count; i += 1) SuspendThread(t->threads[i].handle);
  stop(t);
}

void target_close(Target *t)
{
  bool end = t->pid && !t->attached;
  DEBUG_EVENT e = {.dwDebugEventCode = end ? 0 : EXIT_PROCESS_DEBUG_EVENT};
  if (end) TerminateProcess(t->process, 0);
  if (t->held) ContinueDebugEvent(t->pid, t->held, DBG_CONTINUE);
  else for (uint32_t i = 0; i < t->thread_count; i += 1) ResumeThread(t->threads[i].handle);   /* after a halt */
  if (t->pid && t->attached) DebugActiveProcessStop(t->pid);
  while (e.dwDebugEventCode != EXIT_PROCESS_DEBUG_EVENT && WaitForDebugEvent(&e, 5000)) ContinueDebugEvent(e.dwProcessId, e.dwThreadId, DBG_CONTINUE);
  free(t->threads), free(t->traps), free(t->text), free(t);
}
