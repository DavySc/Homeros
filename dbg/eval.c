#include "eval.h"
#include <string.h>

/* RDI_RegCodeX64 1 to 16 are the general registers, in the order of TargetRegs. */
static bool reg(const TargetRegs *regs, uint64_t code, uint64_t *v)
{
  if (code < 1 || code > 16) return false;
  *v = regs->gp[code - 1];
  return true;
}

/* The address that a location's bytecode computes: e_interpret, as far as the converter writes places (a register
   plus an offset too wide for the location itself: conv.c, reg_plus). Any other operation: no place. */
static bool bytecode(const Engine *e, const Rdi *r, uint64_t at, const TargetRegs *regs, uint64_t *out)
{
  enum { STOP = 0, MEM_READ = 4, REG_READ = 5, CONST_U8 = 11, CONST_U16, CONST_U32, CONST_U64, ADD = 21, TRUNC_SIGNED = 42, DEEP = 8 };
  static const uint8_t operand[64] = {[MEM_READ] = 1, [REG_READ] = 4, [CONST_U8] = 1, [CONST_U16] = 2, [CONST_U32] = 4, [CONST_U64] = 8, [ADD] = 2, [TRUNC_SIGNED] = 1};
  uint64_t stack[DEEP] = {0};
  for (uint32_t n = 0, steps = 0; steps < 64; steps += 1) {
    RdiSpan op = rdi_run(r, RDI_LOCATION_BYTECODE, at, 1);
    uint8_t code = op.n ? *(const uint8_t *)op.p : 0xff;
    uint64_t imm = 0, v = 0;
    if (code == STOP) return *out = stack[0], n == 1;
    RdiSpan p = code < 64 ? rdi_run(r, RDI_LOCATION_BYTECODE, at + 1, operand[code]) : (RdiSpan){0};
    if (code >= 64 || !operand[code]) return false;   /* an operand cut short ends the bytecode: the next operation is none */
    memcpy(&imm, p.p, p.n);
    at += 1 + p.n;
    if (code == REG_READ) {   /* the register's code, how many bytes of it (8 is all a place needs), from which byte on (0) */
      if (imm >> 8 != 8 || !reg(regs, imm & 0xff, &v)) return false;
    } else if (code == MEM_READ) {
      if (n < 1 || imm > 8 || !target_read(e->target, stack[--n], &v, (uint32_t)imm)) return false;
    } else if (code == ADD) {
      if (n < 2) return false;
      n -= 2, v = stack[n] + stack[n + 1];
    } else if (code == TRUNC_SIGNED) {   /* keep the low `imm` bits and repeat the highest of them above */
      if (n < 1 || imm < 1 || imm > 64) return false;
      v = stack[--n];
      if (imm < 64) v = v >> (imm - 1) & 1 ? v | ~(((uint64_t)1 << imm) - 1) : v & (((uint64_t)1 << imm) - 1);
    } else v = imm;
    if (n == DEEP) return false;
    stack[n++] = v;
  }
  return false;
}

/* Where a location (RDI_Location, rdi.h) says a variable is, for code at voff (e_oplist_from_location). */
static EvalPlace place(const Engine *e, const EngineModule *m, uint32_t type, uint64_t l, uint64_t voff, const TargetRegs *regs)
{
  EvalPlace none = {0}, p = {EVAL_MEMORY, m, type, 0};
  if (l >> 56 == 9) {   /* a set: the case that holds at voff */
    uint64_t count = l >> 32 & 0xffffff, first = l & 0xffffffff;
    l = 0;
    for (uint64_t i = 0; i < count && !l; i += 1) {
      const RdiLocationCase *c = rdi_at(&m->rdi, RDI_LOCATION_SETS, RdiLocationCase, first + i);
      if (c->first <= voff && voff < c->opl) l = c->location;
    }
  }
  uint64_t rest = l & 0x00ffffffffffffff, base = 0;
  switch (l >> 56) {
  case 1: return bytecode(e, &m->rdi, rest, regs, &p.at) ? p : none;
  case 3: case 4:   /* at register + offset; 4: at the address stored there */
    if (!reg(regs, rest >> 48, &base)) return none;
    p.at = base + (uint64_t)(int16_t)(rest >> 32);
    return l >> 56 == 3 || target_read(e->target, p.at, &p.at, 8) ? p : none;
  case 5: return rest >> 48 >= 1 && rest >> 48 <= 16 ? (EvalPlace){EVAL_REGISTER, m, type, (rest >> 48) - 1} : none;
  case 6: return p.at = m->image.base + rest, p;
  case 8: return (EvalPlace){EVAL_CONSTANT, m, type, rest};
  default: return none;   /* 7, in the thread's block: not built yet */
  }
}

EvalPlace eval_name(const Engine *e, const TargetRegs *frame, bool called, const char *name, uint64_t len)
{
  const EngineModule *m = engine_module(e, frame->rip - called);
  uint64_t voff = m ? frame->rip - called - m->image.base : 0;
  const RdiScope *scope;
  for (uint32_t s = m ? rdi_vmap(&m->rdi, RDI_SCOPE_VMAP, voff) : 0, n = 0; s && n < 256; s = scope->parent, n += 1) {
    scope = rdi_at(&m->rdi, RDI_SCOPES, RdiScope, s);
    for (uint32_t i = 0; i < scope->local_count; i += 1) {
      const RdiSymbol *y = rdi_at(&m->rdi, RDI_LOCALS, RdiSymbol, scope->local_first + i);
      RdiSpan is = rdi_str(&m->rdi, y->name);
      if (is.n == len && !memcmp(is.p, name, len)) return place(e, m, y->type, y->location, voff, frame);
    }
  }
  for (uint32_t i = 0; i <= 2 * e->module_count + 1; i += 1) {   /* the frame's module, then every module (it again finds nothing new): globals, then constants */
    const EngineModule *g = i < 2 ? m : &e->modules[i / 2 - 1];
    RdiSpan found = g ? rdi_matches(&g->rdi, i % 2 ? RDI_NAMES_CONSTANTS : RDI_NAMES_GLOBALS, name, len) : (RdiSpan){0};
    const RdiSymbol *y = found.n ? rdi_at(&g->rdi, i % 2 ? RDI_CONSTANTS : RDI_GLOBALS, RdiSymbol, *(const uint32_t *)found.p) : 0;
    if (y) return place(e, g, y->type, y->location, 0, frame);
  }
  return (EvalPlace){0};
}

bool eval_read(const Engine *e, const TargetRegs *frame, EvalPlace p, void *bytes, uint32_t n)
{
  if (p.kind == EVAL_MEMORY) return target_read(e->target, p.at, bytes, n);
  /* a register, or a constant: the converter writes each as 8 bytes */
  RdiSpan eight = p.kind == EVAL_CONSTANT ? rdi_run(&p.module->rdi, RDI_LOCATION_CONSTANTS, p.at / 8, 1) : (RdiSpan){&frame->gp[p.at], p.kind == EVAL_REGISTER};
  if (n > 8 || !eight.n) return false;
  return memcpy(bytes, eight.p, n), true;
}
