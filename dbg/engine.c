#include "engine.h"
#include "conv.h"
#include "le.h"
#include "load.h"
#include "x64.h"
#include <string.h>

/* What a trap of the net means when the stepping thread reaches it (RAD: D_TrapFlags). The thread counts as
   having reached it only at the stack depth the step keeps (a deeper call of the same code passes through),
   unless the trap says the depth does not matter. */
enum { IGNORE_SP = 1,     /* any depth */
       STEP_AFTER = 2,    /* run the instruction there, alone */
       SAVE_AFTER = 4,    /* the depth to keep is the one after that instruction */
       LINES = 8,         /* with END: only where that instruction, a call, leads to code a step goes into */
       END = 16,          /* the step is done */
       HELPER = 32 };     /* in the "just my code" helper: first run out of it */

static void *room(void *p, uint32_t *cap, uint32_t n, size_t size)
{
  if (n <= *cap) return p;
  *cap = n + n / 2 + 16;
  if (!(p = realloc(p, *cap * size))) abort();
  return p;
}
/* Room for n elements in an engine's array. */
#define ROOM(e, array, cap, n) ((e)->array = room((e)->array, &(e)->cap, (n), sizeof *(e)->array))

const EngineModule *engine_module(const Engine *e, uint64_t addr)
{
  for (uint32_t i = 0; i < e->module_count; i += 1) if (addr - e->modules[i].image.base < e->modules[i].image.size) return &e->modules[i];
  return 0;
}

static void module_free(EngineModule *m) { free(m->image.table), free(m->path), free(m->rdi_bytes); }

/* Keeps the lists of modules and threads as the target tells of them. A module's debug info is the PDB beside it, under its name. */
static void note(Engine *e, const TargetEvent *ev)
{
  if (ev->kind == TARGET_THREAD) ROOM(e, threads, thread_cap, e->thread_count + 1), e->threads[e->thread_count++] = (EngineThread){ev->thread, 0};
  for (uint32_t i = 0; ev->kind == TARGET_THREAD_GONE && i < e->thread_count; i += 1) if (e->threads[i].id == ev->thread) e->threads[i--] = e->threads[--e->thread_count];
  for (uint32_t i = 0; ev->kind == TARGET_MODULE_GONE && i < e->module_count; i += 1)
    if (e->modules[i].image.base == ev->addr) {   /* the order stays: the first is the program */
      module_free(&e->modules[i]);
      memmove(&e->modules[i], &e->modules[i + 1], (--e->module_count - i) * sizeof *e->modules);
    }
  if (ev->kind != TARGET_MODULE) return;
  ROOM(e, modules, module_cap, e->module_count + 1);
  EngineModule *m = &e->modules[e->module_count];
  *m = (EngineModule){0};
  if (!unwind_module(target_read, e->target, ev->addr, &m->image)) return;   /* mapped and gone again while the target ran on */
  e->module_count += 1;
  size_t n = strlen(ev->text);
  char *pdb_path = malloc(n + 5);
  if (!pdb_path || !(m->path = malloc(n + 1))) abort();
  memcpy(m->path, ev->text, n + 1), memcpy(pdb_path, ev->text, n + 1);
  char *dot = strrchr(pdb_path, '.');
  strcpy(dot ? dot : pdb_path + n, ".pdb");
  uint64_t size = 0, rdi_size = 0;
  void *pdb = load(pdb_path, &size);
  free(pdb_path);
  if (!pdb) return;
  m->rdi_bytes = rdi_from_pdb(pdb, size, &rdi_size);
  free(pdb);
  if (m->rdi_bytes && !rdi_open(&m->rdi, m->rdi_bytes, rdi_size)) free(m->rdi_bytes), m->rdi_bytes = 0, m->rdi = (Rdi){0};
}

bool engine_open(Engine *e, const char *command_line, uint32_t pid, uint32_t *first_thread)
{
  TargetEvent first;
  *e = (Engine){0};
  if (!(e->target = target_open(command_line, pid, &first))) return false;
  note(e, &first), note(e, &(TargetEvent){.kind = TARGET_THREAD, .thread = *first_thread = first.thread});
  return true;
}

void engine_close(Engine *e)
{
  target_close(e->target);
  for (uint32_t i = 0; i < e->module_count; i += 1) module_free(&e->modules[i]);
  free(e->modules), free(e->threads), free(e->net), free(e->traps), free(e->ranges), free(e->code);
}

uint64_t engine_function(const EngineModule *m, const char *name, uint64_t n)
{
  RdiSpan found = rdi_matches(&m->rdi, RDI_NAMES_PROCEDURES, name, n);
  if (!found.n) return 0;
  const RdiScope *scope = rdi_at(&m->rdi, RDI_SCOPES, RdiScope, rdi_at(&m->rdi, RDI_PROCEDURES, RdiSymbol, *(const uint32_t *)found.p)->root_scope);
  return m->image.base + *rdi_at(&m->rdi, RDI_SCOPE_VOFFS, uint64_t, scope->voff_first);
}

uint64_t engine_entry(const Engine *e)
{
  static const char *const names[] = {"WinMain", "wWinMain", "main", "wmain"};
  const EngineModule *m = e->modules;
  uint8_t at[4];
  for (uint32_t i = 0; i < 4; i += 1) {
    uint64_t addr = engine_function(m, names[i], strlen(names[i]));
    if (addr) return addr;
  }
  return target_read(e->target, m->image.base + 0x3c, at, 4) && target_read(e->target, m->image.base + le32(at) + 24 + 16, at, 4) ? m->image.base + le32(at) : 0;
}

enum { WAIT_MS = 10 };   /* a halt takes at most this long to be seen (RAD: 100, and 30 while the user waits) */

/* Lets the target run with the first `count` of e->traps, or one thread alone for one instruction, until there is
   something to stop for. An exception is one when the program's handlers have left it, or at once for the few kinds
   that are faults whoever handles them (RAD's defaults, d_exception_code_kind_default_enable_table); the others
   go to the program and it runs on. An exception stopped for is not given to the program: the instruction that
   raised it runs again. */
static TargetEvent run_on(Engine *e, uint32_t count, uint32_t step_thread)
{
  static const uint32_t at_once[] = {0x40010005 /* control-C */, 0x40010008 /* control-break */, 0xc0000005 /* access violation */,
                                     0xc0000008 /* invalid handle */, 0xc0000420 /* assertion */, 0xe073616e /* the sanitizer's access violation */};
  for (;;) {
    TargetEvent ev;
    target_resume(e->target, e->traps, count, step_thread);
    while (!target_wait(e->target, WAIT_MS, &ev)) {
      note(e, &ev);
      if (e->halt && e->halt(e->halt_arg)) return target_halt(e->target), (TargetEvent){.kind = TARGET_HALT};
    }
    bool stop = ev.kind != TARGET_EXCEPTION || ev.unhandled;
    for (uint32_t i = 0; i < sizeof at_once / sizeof *at_once; i += 1) stop = stop || ev.code == at_once[i];
    if (!stop) continue;
    if (ev.kind == TARGET_EXCEPTION) target_swallow(e->target);
    return ev;
  }
}

/* One instruction of one thread, alone. */
static TargetEvent step_one(Engine *e, uint32_t thread) { return run_on(e, 0, thread); }

static void net_add(Engine *e, uint64_t addr, uint32_t flags)
{
  ROOM(e, net, net_cap, e->net_count + 1);
  e->net[e->net_count++] = (EngineTrap){addr, flags};
}

/* The target's code at addr into e->code: n bytes, or as many as can be read. Returns how many. */
static uint32_t code_at(Engine *e, uint64_t addr, uint32_t n)
{
  ROOM(e, code, code_cap, n);
  while (n && !target_read(e->target, addr, e->code, n)) n -= 1;
  return n;
}

/* Whether one of the first `count` ranges in e->ranges (first and one-past-last offset each) holds off. */
static bool in_ranges(const Engine *e, uint32_t count, uint64_t off)
{
  for (uint32_t i = 0; i < count; i += 1) if (off >= e->ranges[2 * i] && off < e->ranges[2 * i + 1]) return true;
  return false;
}

enum { CODE_MAX = 1 << 20 };   /* no line and no scope is this long: a longer one is a broken table */

/* Step over an instruction (d_trap_net_from_thread__step_over_inst): a call or a repeated instruction runs to the one after it. */
static void inst_net(Engine *e, uint64_t rip)
{
  X64Inst x = x64_decode(e->code, code_at(e, rip, 15), rip);
  if (x.flow == X64_CALL || x.repeats) net_add(e, rip + x.size, END);
}

/* Whether a line step goes into a call that leads to addr: where there are lines (the row behind a block of lines
   has none), and not into the stack probe every large frame calls first, which Visual Studio's list of functions
   not to step into names (default.natstepfilter; the rest of that list is not recorded). */
static bool goes_into(const Engine *e, uint64_t addr)
{
  const EngineModule *m = engine_module(e, addr);
  if (!m || !rdi_row(&m->rdi, addr - m->image.base).line.line) return false;
  RdiSpan name = rdi_str(&m->rdi, rdi_at(&m->rdi, RDI_PROCEDURES, RdiSymbol, rdi_at(&m->rdi, RDI_SCOPES, RdiScope, rdi_vmap(&m->rdi, RDI_SCOPE_VMAP, addr - m->image.base))->proc)->name);
  return !(name.n == 8 && !memcmp(name.p, "__chkstk", 8));
}

/* Whether addr is in code that is not the user's: the compiler's own runtime, by where its source was (Visual Studio's
   list, default.natjmc: "*\vctools\crt\*"; its other entries, the C++ library's headers among them, have no subject yet). In a program built with MSVC's "just my code" (/JMC) a step from the user's code does not end
   there; the C library, whose source is elsewhere, is stepped as the user's (Visual Studio on mine_jmc and calls_jmc;
   knowledge base 03, "Just my code: the mechanism"). */
static bool theirs(const Engine *e, uint64_t addr)
{
  const EngineModule *m = engine_module(e, addr);
  char path[1024];
  if (!m) return false;
  uint64_t n = rdi_path(&m->rdi, rdi_at(&m->rdi, RDI_SOURCE_FILES, RdiSourceFile, rdi_row(&m->rdi, addr - m->image.base).line.file)->path_node, path, sizeof path);
  path[n < sizeof path ? n : sizeof path - 1] = 0;
  return strstr(path, "/vctools/crt/");
}

/* /JMC puts a call of a helper behind the first line of each function, under the made-up line number 0xf00f00. The
   helper tells the debugger that a thread came into one of the user's functions: it reaches its nop only on the
   thread whose id the debugger wrote into __DebuggerCurrentSteppingThreadId. Writes it in every module that has one,
   and adds the nops to the net. It stays written: outside a step no trap is on a nop. */
static void watch(Engine *e, uint32_t thread)
{
  for (uint32_t i = 0; i < e->module_count; i += 1) {
    const EngineModule *m = &e->modules[i];
    RdiSpan found = rdi_matches(&m->rdi, RDI_NAMES_GLOBALS, "__DebuggerCurrentSteppingThreadId", 33);
    for (uint64_t k = 0; k < found.n; k += 1) {
      uint64_t at = rdi_at(&m->rdi, RDI_GLOBALS, RdiSymbol, ((const uint32_t *)found.p)[k])->location;
      if (at >> 56 == 6) target_write(e->target, m->image.base + (at & 0xffffffffffffff), &thread, 4);   /* the linker's name for it has no place */
    }
    uint64_t helper = engine_function(m, "__CheckForDebuggerJustMyCode", 28);
    for (uint32_t n = helper ? code_at(e, helper, 256) : 0, at = 0; at < n;) {
      X64Inst x = x64_decode(e->code + at, n - at, helper + at);
      if (!x.size) break;
      if (e->code[at] == 0x90) { net_add(e, helper + at, HELPER | END | IGNORE_SP); break; }
      at += x.size;
    }
  }
}

/* Where a piece of a line ends: behind what MSVC's "just my code" puts after it under the made-up
   line number 0xf00f00, "never step into": a call of a function that has lines. As cdb, a step neither ends on it nor
   goes into its call; RAD does both (knowledge base 03, "A second reference"). */
static uint64_t line_end(const Rdi *rdi, RdiRow row)
{
  RdiRow after = rdi_row(rdi, row.opl);
  return after.line.line == 0xf00f00 ? after.opl : row.opl;
}

/* Step over or into a line (d_trap_net_from_thread__step_over_line, __step_into_line): a trap wherever the thread
   can leave the line's code. With no line at rip the net stays empty, and the step is one instruction. */
static void line_net(Engine *e, uint64_t rip, bool into)
{
  const EngineModule *m = engine_module(e, rip);
  uint64_t base = m ? m->image.base : 0;
  RdiRow row = m ? rdi_row(&m->rdi, rip - base) : (RdiRow){0};
  if (!row.opl || row.opl - row.first > CODE_MAX) return;
  /* Every piece of code of the same source line: to jump into one of them is not to leave the line, and the thread
     can leave the line from each (RAD looks only at the piece the thread is in, and a step that leaves by another
     runs to the program's end: mine_jmc.into, the C library's parse_integer; Visual Studio stops). The row itself is
     one more where the source line map has none for it: the code between two blocks of lines (a constructor the
     compiler made). */
  RdiSpan starts = rdi_voffs_from_line(&m->rdi, row.line.file, row.line.line);
  uint32_t count = (uint32_t)starts.n;
  ROOM(e, ranges, range_cap, 2 * count + 2);
  for (uint32_t i = 0; i < count; i += 1) e->ranges[2 * i] = ((const uint64_t *)starts.p)[i], e->ranges[2 * i + 1] = line_end(&m->rdi, rdi_row(&m->rdi, e->ranges[2 * i]));
  if (!in_ranges(e, count, row.first)) e->ranges[2 * count] = row.first, e->ranges[2 * count + 1] = line_end(&m->rdi, row), count += 1;
  for (uint32_t i = 0; i < count; i += 1) {
    uint64_t lo = e->ranges[2 * i], hi = e->ranges[2 * i + 1];
    uint32_t n = hi - lo > CODE_MAX ? 0 : code_at(e, base + lo, (uint32_t)(hi - lo));
    for (uint32_t at = 0; at < n;) {
      uint64_t addr = base + lo + at;
      X64Inst x = x64_decode(e->code + at, n - at, addr);
      if (!x.size) break;
      at += x.size;
      uint64_t to = x.known ? x.target : 0;
      if (x.flow == X64_CALL && !into) net_add(e, addr, STEP_AFTER);   /* into the call, and on: the trap behind the call waits at this depth */
      else if (x.flow != X64_OTHER) {
        /* A jump, branch or return: the step ends where it goes, when that is known, else after the instruction has
           run; not when it lands in the line. Stepping into, a call: it is run, and the step ends where it led if a
           step goes into that (as Visual Studio; RAD goes into the line's last call wherever it leads: knowledge base
           03, "The reference that decides"). */
        bool call = x.flow == X64_CALL;
        if (call) to = 0;   /* a trap where the function begins would also end the step when another call's code came there */
        if (!in_ranges(e, count, to - base)) net_add(e, to ? to : addr, END | (to ? 0 : STEP_AFTER) | (call ? LINES : 0));
      } else if (x.moves_sp) net_add(e, addr, STEP_AFTER | SAVE_AFTER);
    }
    net_add(e, base + hi, END);   /* running off the end of the piece (the converter made one piece of pieces that touch) */
  }
}

/* Step out (d_trap_net_from_thread__step_out_scope): out of the innermost scope the thread is in, which is a block
   or an inlined call before it is the function: a trap where a jump leaves the scope, behind each of its ranges, and
   where the function returns to. */
static void out_net(Engine *e, TargetRegs regs)
{
  const EngineModule *m = engine_module(e, regs.rip);
  uint64_t base = m ? m->image.base : 0;
  const RdiScope *scope = m ? rdi_at(&m->rdi, RDI_SCOPES, RdiScope, rdi_vmap(&m->rdi, RDI_SCOPE_VMAP, regs.rip - base)) : 0;
  RdiSpan bounds = m ? rdi_run(&m->rdi, RDI_SCOPE_VOFFS, scope->voff_first, scope->voff_opl - scope->voff_first) : (RdiSpan){0};
  /* as RAD: every two neighbours in the scope's list of bounds make a range, so the gap between two ranges counts as scope too */
  uint32_t count = bounds.n ? (uint32_t)bounds.n - 1 : 0;
  ROOM(e, ranges, range_cap, 2 * count);
  for (uint32_t i = 0; i < count; i += 1) memcpy(&e->ranges[2 * i], (const uint64_t *)bounds.p + i, 16);
  for (uint32_t i = 0; i < count; i += 1) {
    uint64_t lo = e->ranges[2 * i], hi = e->ranges[2 * i + 1];
    if (hi < lo || hi - lo > CODE_MAX) continue;
    uint32_t n = code_at(e, base + lo, (uint32_t)(hi - lo));
    for (uint32_t at = 0; at < n;) {
      X64Inst x = x64_decode(e->code + at, n - at, base + lo + at);
      if (!x.size) break;
      at += x.size;
      if (x.flow != X64_CALL && x.known && !in_ranges(e, count, x.target - base)) net_add(e, x.target, END | IGNORE_SP);
    }
    if (!in_ranges(e, count, hi)) net_add(e, base + hi, END | IGNORE_SP);
  }
  if (unwind_step(target_read, e->target, m ? &m->image : 0, &regs)) net_add(e, regs.rip, END | IGNORE_SP);
}

/* RAD: d_ctrl_thread__run. */
static TargetEvent run(Engine *e, int how, uint32_t thread, const uint64_t *breakpoints, uint32_t count)
{
  TargetRegs regs = {0};
  TargetEvent ev = {0};
  uint64_t start = 0;   /* where the stepping thread stands, when the net has a trap there */
  e->net_count = 0;
  if (how != ENGINE_RUN) {
    if (!target_regs(e->target, thread, &regs)) return ev;   /* no such thread: nothing runs */
    if (how == ENGINE_STEP_OVER_INST) inst_net(e, regs.rip);
    else if (how == ENGINE_STEP_INTO_LINE || how == ENGINE_STEP_OVER_LINE) line_net(e, regs.rip, how == ENGINE_STEP_INTO_LINE);
    else if (how == ENGINE_STEP_OUT) out_net(e, regs);
    if (!e->net_count) return step_one(e, thread);
    for (uint32_t i = 0; i < e->net_count; i += 1) if (e->net[i].addr == regs.rip) start = regs.rip;
  }
  /* A thread that stands on a breakpoint where its stop has been reported would hit it again at once: each such
     thread first runs the instruction there, alone. A thread nobody was told of stands there too (it ran the trap
     during another thread's stop and was put back, or was stopped just before the trap): it is left to hit it now.
     RAD steps every thread that stands on a breakpoint; specs/Hit.tla shows the hit that loses.
     The stepping thread leaves its place whether told or not, and not by this step when the net has a trap there: that trap
     says what its instruction does to the step (it moves the stack pointer, or is the call to step over), so the thread
     is left to run into it (RAD: target_thread_is_on_user_bp_and_trap_net_trap). */
  for (uint32_t i = 0; i < e->thread_count; i += 1) {
    TargetRegs r;
    uint32_t id = e->threads[i].id;
    bool stuck = false, steps = how != ENGINE_RUN && id == thread;
    for (uint32_t k = 0; k < count && target_regs(e->target, id, &r) && (steps || r.rip == e->threads[i].told); k += 1) stuck = stuck || breakpoints[k] == r.rip;
    if (stuck && !(steps && start) && (ev = step_one(e, id)).kind != TARGET_STEP) return ev;
  }
  for (uint32_t i = 0; i < e->thread_count; i += 1) e->threads[i].told = 0;   /* now all run: one that comes to stand there again has not been told (Hit.tla, "kept") */
  uint64_t depth = regs.gp[4];
  const EngineModule *m = engine_module(e, regs.rip);
  bool user = m && engine_function(m, "__CheckForDebuggerJustMyCode", 28) && !theirs(e, regs.rip);   /* built with /JMC, and begun in the user's code */
  for (uint32_t planted = 0;;) {   /* the net can grow on the way (watch) */
    ROOM(e, traps, trap_cap, count + e->net_count);
    for (; planted < count + e->net_count; planted += 1) e->traps[planted] = planted < count ? breakpoints[planted] : e->net[planted - count].addr;
    ev = run_on(e, count + e->net_count, 0);
    if (ev.kind != TARGET_TRAP) return ev;
    uint32_t flags = 0;
    if (ev.thread == thread && ev.addr == start) start = 0;   /* the step's own first trap, not the breakpoint at the same place */
    else for (uint32_t i = 0; i < count; i += 1) if (breakpoints[i] == ev.addr) return ev;
    for (uint32_t i = 0; i < e->net_count; i += 1) if (e->net[i].addr == ev.addr) flags |= e->net[i].flags;
    /* another thread in the net, or this one at another depth: over the trap, alone, and on */
    bool reached = ev.thread == thread && target_regs(e->target, thread, &regs) && (flags & IGNORE_SP || regs.gp[4] == depth);
    if ((!reached || flags & STEP_AFTER) && (ev = step_one(e, ev.thread)).kind != TARGET_STEP) return ev;
    if (!reached) continue;
    if (flags & SAVE_AFTER && target_regs(e->target, thread, &regs)) depth = regs.gp[4];
    if (flags & HELPER) {   /* to the function that called it */
      do ev = step_one(e, thread); while (ev.kind == TARGET_STEP && theirs(e, ev.addr));
      if (ev.kind != TARGET_STEP) return ev;
    }
    if (!(flags & END)) continue;
    /* where a call leads is known when it has run: into code no step goes into, the thread runs on, the net as it is;
       so it does from the user's code into code that is not, until it comes to a function of the user's */
    if (!(flags & LINES && !goes_into(e, ev.addr)) && !(user && theirs(e, ev.addr))) return (TargetEvent){.kind = TARGET_STEP, .thread = thread, .addr = ev.addr};
    if (user) watch(e, thread);
    if (!(flags & STEP_AFTER) && (ev = step_one(e, thread)).kind != TARGET_STEP) return ev;   /* off the trap it stands on */
  }
}

TargetEvent engine_run(Engine *e, int how, uint32_t thread, const uint64_t *breakpoints, uint32_t count)
{
  TargetEvent ev = run(e, how, thread, breakpoints, count);
  TargetRegs regs;
  for (uint32_t i = 0; i < e->thread_count; i += 1) if (e->threads[i].id == ev.thread && target_regs(e->target, ev.thread, &regs)) e->threads[i].told = regs.rip;
  return ev;
}
