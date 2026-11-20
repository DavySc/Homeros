/* The debugger's engine: a target process with its modules and their debug info, and the one
   function that runs it: freely to a breakpoint, or as a step (the knowledge base, 03: "Running
   and stepping: the one algorithm" and "Trap nets"; RAD's d_ctrl_thread__run and
   d_trap_net_from_thread__*). A step is a run with a net of traps around the place the thread is
   in; what each trap means when the thread reaches it is written on the trap.
   Every call comes from one thread. */
#ifndef DBG_ENGINE_H
#define DBG_ENGINE_H
#include "rdi.h"
#include "target.h"
#include "unwind.h"

/* A module of the target: where it is mapped, its file. Without debug info rdi is empty: every lookup in it finds nothing. */
typedef struct { UnwindModule image; char *path; Rdi rdi; void *rdi_bytes; } EngineModule;
typedef struct { uint64_t addr; uint32_t flags; } EngineTrap;
/* told: where the thread stood when a stop of its own was last reported, until it runs (specs/Hit.tla). */
typedef struct { uint32_t id; uint64_t told; } EngineThread;
typedef struct {
  Target *target;
  EngineModule *modules;   /* the first is the program */
  EngineThread *threads;
  /* room that grows to the largest step and stays: the net, the addresses a run plants, a line's ranges, its code */
  EngineTrap *net;
  uint64_t *traps, *ranges;
  uint8_t *code;
  /* asked while the target runs, between the waits for it: true to stop it now (the engine thread looks at its command queue). Null: never. */
  bool (*halt)(void *arg);
  void *halt_arg;
  uint32_t module_count, module_cap, thread_count, thread_cap, net_count, net_cap, trap_cap, range_cap, code_cap;
} Engine;

enum { ENGINE_RUN, ENGINE_STEP_INTO_INST, ENGINE_STEP_OVER_INST, ENGINE_STEP_INTO_LINE, ENGINE_STEP_OVER_LINE, ENGINE_STEP_OUT };

/* Starts command_line, or attaches to pid when it is null; the target is stopped. first_thread: the thread it began with. */
bool engine_open(Engine *e, const char *command_line, uint32_t pid, uint32_t *first_thread);
/* The module that holds addr, or null. */
const EngineModule *engine_module(const Engine *e, uint64_t addr);
/* Where the module's function of that name begins; 0 when it has none. */
uint64_t engine_function(const EngineModule *m, const char *name, uint64_t n);
/* Where the program's own code begins: WinMain, wWinMain, main or wmain, the first there is, else the entry its header names. */
uint64_t engine_entry(const Engine *e);
/* Lets the target run until it stops, and says why: TARGET_TRAP on a breakpoint (addr), TARGET_STEP when
   the step of `thread` is done (addr: where it stands), TARGET_HALT when e->halt said so, or what else stopped it (target.h). how ENGINE_RUN: thread is not used. */
TargetEvent engine_run(Engine *e, int how, uint32_t thread, const uint64_t *breakpoints, uint32_t count);
void engine_close(Engine *e);
#endif
