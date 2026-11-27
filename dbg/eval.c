#include "eval.h"
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

/* RDI_RegCodeX64 1 to 16 are the general registers, in the order of TargetRegs. */
static bool reg(const TargetRegs *regs, uint64_t code, uint64_t *v)
{
  if (code < 1 || code > 16) return false;
  *v = regs->gp[code - 1];
  return true;
}

/* The low `bits` bits of v; with a sign, the highest of them repeated above. */
static uint64_t narrow(uint64_t v, uint64_t bits, bool sign)
{
  if (bits >= 64) return v;
  uint64_t low = ((uint64_t)1 << bits) - 1;
  return sign && bits && (v >> (bits - 1) & 1) ? v | ~low : v & low;
}

/* ---- types ---- */

enum { VOID = 1, CHAR = 4, UCHAR = 7, U32 = 0xc, U64 = 0xd, S8 = 0x11, S16, S32, S64, BOOL = 0x18, F32 = 0x1b, F64 = 0x1e };   /* RDI_TypeKind */
EvalShape eval_shape(EvalType t)
{
  enum { SIGN = 0x10, FLOAT = 0x20 };
  static const uint8_t basic[F64 + 1] = {[2] = 8, [3] = 4 | SIGN, [CHAR] = 1 | SIGN, 2, 4, [UCHAR] = 1, 2, 4, 1, 2, [U32] = 4, [U64] = 8,
                                         [S8] = 1 | SIGN, [S16] = 2 | SIGN, [S32] = 4 | SIGN, [S64] = 8 | SIGN, [BOOL] = 1, [F32] = 4 | FLOAT, [F64] = 8 | FLOAT};
  EvalShape s = {0};
  if (t.stars) return (EvalShape){EVAL_ADDRESS, .size = 8, .to = {t.module, t.idx, t.stars - 1}};
  for (uint32_t hops = 0; hops < 64; hops += 1) {   /* a file whose types go round is not followed for ever */
    const RdiType *y = t.module ? rdi_at(&t.module->rdi, RDI_TYPE_NODES, RdiType, t.idx) : NULL;
    uint32_t kind = y ? y->kind : t.idx;
    if (kind < sizeof basic) {
      if (basic[kind]) s.form = basic[kind] & FLOAT ? EVAL_REAL : EVAL_INT, s.size = basic[kind] & 15, s.sign = basic[kind] & SIGN, s.truth = kind == BOOL, s.to = t;
      return s;
    }
    if (!y) return s;
    s.to = (EvalType){t.module, y->a, 0};
    switch (kind) {
    case 0x1000: t.idx = y->a; break;
    case 0x2003: s.names = y->c; /* fallthrough */
    case 0x2004: t.idx = y->b; break;
    case 0xf000: s.first = y->b, s.count = y->c, t.idx = y->a; break;
    case 0x1001: return s.form = EVAL_ADDRESS, s.size = 8, s;
    case 0x1002: case 0x1003: return s.form = EVAL_REFERENCE, s.size = 8, s;
    case 0x1004: return s.form = EVAL_ROW, s.size = y->byte_size, s;
    case 0x2000: case 0x2001: case 0x2002: return s.form = EVAL_RECORD, s.size = y->byte_size, s.to.idx = y->c, s;
    default: return s;
    }
  }
  return (EvalShape){0};
}

/* ---- places ---- */

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
      v = narrow(stack[--n], imm, true);
    } else v = imm;
    if (n == DEEP) return false;
    stack[n++] = v;
  }
  return false;
}

/* Where a location (RDI_Location, rdi.h) says a variable is, for code at voff (e_oplist_from_location). */
static EvalValue place(const Engine *e, const EngineModule *m, uint32_t type, uint64_t l, uint64_t voff, const TargetRegs *regs)
{
  EvalValue none = {0}, p = {EVAL_MEMORY, {m, type, 0}, 0};
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
  case 5:
    if (rest >> 48 < 1 || rest >> 48 > 16) return none;
    p.kind = EVAL_REGISTER, p.at = (rest >> 48) - 1;
    break;
  case 6: return p.at = m->image.base + rest, p;
  case 8: p.kind = EVAL_CONSTANT, p.at = rest; break;
  default: return none;   /* 7, in the thread's block: not built yet */
  }
  /* a register or a constant has 8 bytes: a larger variable is not there (clang writes a struct it has taken apart as the constant 0) */
  return eval_shape(p.type).size <= 8 ? p : none;
}


/* The first n bytes of the value: any number of them from memory, up to 8 otherwise. */
static bool eval_read(const Engine *e, const TargetRegs *frame, EvalValue v, void *bytes, uint32_t n)
{
  if (v.kind == EVAL_MEMORY) return target_read(e->target, v.at, bytes, n);
  /* a register, a constant (the converter writes each as 8 bytes), or the value itself */
  RdiSpan eight = v.kind == EVAL_CONSTANT ? rdi_run(&v.type.module->rdi, RDI_LOCATION_CONSTANTS, v.at / 8, 1) : (RdiSpan){v.kind == EVAL_REGISTER ? &frame->gp[v.at] : &v.at, v.kind != EVAL_NONE};
  if (n > 8 || !eight.n) return false;
  return memcpy(bytes, eight.p, n), true;
}

/* ---- values ---- */

typedef struct { const Engine *e; const TargetRegs *frame; const EngineModule *module; uint64_t voff; uint32_t scope;   /* the frame's instruction, and the innermost scope around it that is the frame's */
                 const char *p, *error;
                 uint32_t dead,    /* not 0 inside a part whose value C does not work out (the right of a decided && or ||, the side of ? : not chosen, sizeof's): parsed and typed, nothing read */
                          deep; } Parse;

static EvalValue fail(Parse *P, const char *why)
{
  if (!P->error) P->error = why;
  return (EvalValue){0};
}

static EvalValue value(uint32_t kind, uint64_t bits) { return (EvalValue){EVAL_VALUE, {NULL, kind, 0}, bits}; }

static EvalValue number(double d)
{
  EvalValue v = value(F64, 0);
  return memcpy(&v.at, &d, 8), v;
}

/* The number a loaded value is. */
static double real(EvalValue v, EvalShape s)
{
  double d;
  float f;
  if (s.form != EVAL_REAL) return s.sign ? (double)(int64_t)v.at : (double)v.at;
  if (s.size == 4) return memcpy(&f, &v.at, 4), (double)f;
  return memcpy(&d, &v.at, 8), d;
}

static bool nonzero(EvalValue v, EvalShape s) { return s.form == EVAL_REAL ? real(v, s) != 0 : v.at != 0; }

/* A reference stands for what it refers to. (Only a variable can be one here: clang gives a parameter passed as a pointer to a copy the type
   "reference to" what it is. A C++ program's members and pointers to references are not built.) */
static EvalValue settle(Parse *P, EvalValue v)
{
  EvalShape s = eval_shape(v.type);
  uint64_t at = 0;
  if (!v.kind || s.form != EVAL_REFERENCE) return v;
  if (!P->dead && !eval_read(P->e, P->frame, v, &at, 8)) return fail(P, "cannot be read");
  return (EvalValue){EVAL_MEMORY, s.to, at};
}

/* What C reads at a place: a number or an address, its sign carried to 64 bits; of an array, the address of its first element. */
static EvalValue load(Parse *P, EvalValue v)
{
  EvalShape s = eval_shape(v.type);
  uint64_t bits = 0;
  if (!v.kind) return v;
  if (s.form == EVAL_ROW && v.kind == EVAL_MEMORY) return (EvalValue){EVAL_VALUE, {s.to.module, s.to.idx, s.to.stars + 1}, v.at};
  if (s.form != EVAL_INT && s.form != EVAL_REAL && s.form != EVAL_ADDRESS) return fail(P, "not a number");
  if (!P->dead && !eval_read(P->e, P->frame, v, &bits, s.size)) return fail(P, "cannot be read");
  if (!s.count) return (EvalValue){EVAL_VALUE, v.type, narrow(bits, s.size * 8, s.sign)};
  /* a bit field of fewer bits than an int is an int (C's promotion); else it is its whole number's type */
  return (EvalValue){EVAL_VALUE, s.count < 32 ? (EvalType){NULL, S32, 0} : s.to, narrow(bits >> (s.first & 63), s.count, s.sign)};
}

/* The address held at v: what is there. */
static EvalValue deref(Parse *P, EvalValue v)
{
  v = load(P, v);
  EvalShape s = eval_shape(v.type);
  if (!v.kind) return v;
  if (s.form != EVAL_ADDRESS) return fail(P, "not an address");
  return (EvalValue){EVAL_MEMORY, s.to, v.at};
}

/* A real number as a whole one: as the processor's cvttsd2si does it, and above that for the unsigned (C leaves these cases open). */
static uint64_t whole(double d)
{
  if (d >= -0x1p63 && d < 0x1p63) return (uint64_t)(int64_t)d;
  return d >= 0x1p63 && d < 0x1p64 ? (uint64_t)d : (uint64_t)1 << 63;
}

/* A loaded value as another type: C's conversions. */
static EvalValue convert(Parse *P, EvalValue v, EvalType type)
{
  EvalShape from = eval_shape(v.type), s = eval_shape(type);
  if (!v.kind) return v;
  if ((s.form == EVAL_REAL && from.form == EVAL_ADDRESS) || (s.form == EVAL_ADDRESS && from.form == EVAL_REAL)) return fail(P, "no such conversion");
  if (s.form == EVAL_REAL) {
    double d = real(v, from);
    float f = (float)d;
    v.at = 0;
    if (s.size == 4) memcpy(&v.at, &f, 4); else memcpy(&v.at, &d, 8);
  } else if (s.form == EVAL_INT || s.form == EVAL_ADDRESS) {
    v.at = s.truth ? nonzero(v, from) : narrow(from.form == EVAL_REAL ? whole(real(v, from)) : v.at, s.size * 8, s.sign);
  } else return fail(P, "no such conversion");
  return v.type = type, v;
}

/* The type two numbers meet in: C's usual arithmetic conversions, where long has 4 bytes. */
static uint32_t common(EvalShape x, EvalShape y)
{
  if (x.form == EVAL_REAL || y.form == EVAL_REAL) return (x.form == EVAL_REAL && x.size == 8) || (y.form == EVAL_REAL && y.size == 8) ? F64 : F32;
  uint32_t size = x.size > y.size ? x.size : y.size;
  if (size < 4) return S32;
  bool sign = (x.size < size || x.sign) && (y.size < size || y.sign);
  return size == 8 ? (sign ? S64 : U64) : sign ? S32 : U32;
}

/* a `op` b, both loaded. op: the operator's character, or for one of two characters the letter in the table of `binary`. */
static EvalValue arith(Parse *P, int op, EvalValue a, EvalValue b)
{
  EvalShape x = eval_shape(a.type), y = eval_shape(b.type);
  bool compare = strchr("<>lgen", op), shift = op == 'L' || op == 'R';
  if (!a.kind || !b.kind) return (EvalValue){0};
  if (x.form == EVAL_INT && y.form == EVAL_ADDRESS && op == '+') return arith(P, op, b, a);
  if ((x.form == EVAL_ADDRESS || y.form == EVAL_ADDRESS) && (!compare || x.form == EVAL_REAL || y.form == EVAL_REAL)) {
    uint64_t step = x.form == EVAL_ADDRESS ? eval_shape(x.to).size : 0;   /* 0 also for an address of nothing with a size */
    if (step && y.form == EVAL_INT && (op == '+' || op == '-')) return a.at += (op == '+' ? b.at : -b.at) * step, a;
    if (step && y.form == EVAL_ADDRESS && op == '-') return value(S64, (uint64_t)((int64_t)(a.at - b.at) / (int64_t)step));
    return fail(P, "not for an address");
  }
  if ((x.form == EVAL_REAL || y.form == EVAL_REAL) && !compare && !strchr("+-*/", op)) return fail(P, "not for a real number");
  EvalType type = {NULL, common(x, shift ? x : y), 0};
  EvalShape s = eval_shape(type);
  a = convert(P, a, type);
  b = convert(P, b, type);   /* a shift's count too: its low bits stay */
  if (P->dead) return value(compare ? S32 : type.idx, 0);
  uint64_t u = a.at, w = b.at, r = 0, count = w & (s.size * 8 - 1);   /* a shift by the count's low bits, as the processor does it (C leaves more open) */
  int64_t i = (int64_t)u, j = (int64_t)w;
  double d = s.form == EVAL_REAL ? real(a, s) : 0, f = s.form == EVAL_REAL ? real(b, s) : 0;
  bool less = s.form == EVAL_REAL ? d < f : s.sign ? i < j : u < w, more = s.form == EVAL_REAL ? d > f : s.sign ? i > j : u > w, equal = s.form == EVAL_REAL ? d == f : u == w;
  switch (op) {
  case '<': return value(S32, less);
  case '>': return value(S32, more);
  case 'l': return value(S32, less || equal);
  case 'g': return value(S32, more || equal);
  case 'e': return value(S32, equal);
  case 'n': return value(S32, !equal);
  case '+': r = u + w, d += f; break;
  case '-': r = u - w, d -= f; break;
  case '*': r = u * w, d *= f; break;
  case '/': case '%':
    if (s.form == EVAL_REAL) { d /= f; break; }
    if (!w) return fail(P, "division by zero");
    if (!s.sign) r = op == '/' ? u / w : u % w;
    else if (j == -1) r = op == '/' ? -u : 0;   /* the least number by -1 has no quotient in C; this is the one that wraps */
    else r = (uint64_t)(op == '/' ? i / j : i % j);
    break;
  case '&': r = u & w; break;
  case '^': r = u ^ w; break;
  case '|': r = u | w; break;
  case 'L': r = u << count; break;
  default: r = s.sign ? (uint64_t)(i >> count) : u >> count; break;   /* 'R' */
  }
  return convert(P, s.form == EVAL_REAL ? number(d) : value(type.idx, r), type);
}

/* ---- the text ---- */

static bool eat(Parse *P, const char *text)
{
  size_t n = strlen(text);
  while (*P->p == ' ') P->p += 1;
  if (strncmp(P->p, text, n)) return false;
  return P->p += n, true;
}

/* The name at the text's position, or nothing. */
static RdiSpan word(Parse *P)
{
  eat(P, "");
  const char *from = P->p;
  while (*P->p == '_' || isalpha((unsigned char)*P->p) || (P->p > from && isdigit((unsigned char)*P->p))) P->p += 1;
  return (RdiSpan){from, (uint64_t)(P->p - from)};
}

static bool same(RdiSpan a, const char *text, uint64_t n) { return a.n == n && !memcmp(a.p, text, n); }

/* The module to look in i-th: the frame's, then every module (the frame's again finds nothing new). */
static const EngineModule *nth(const Parse *P, uint32_t i) { return i ? &P->e->modules[i - 1] : P->module; }

/* A name's index in one of a module's tables of names; 0: not there. */
static uint32_t known(const EngineModule *m, uint32_t names, RdiSpan id)
{
  RdiSpan found = m ? rdi_matches(&m->rdi, names, id.p, id.n) : (RdiSpan){0};
  return found.n ? *(const uint32_t *)found.p : 0;
}

/* The variables of the scopes around the frame's instruction, one at each call, from the innermost scope out; null after the last. */
typedef struct { uint32_t scope, hops; uint64_t i; } Locals;

static const RdiSymbol *local(const Parse *P, Locals *l)
{
  for (; l->scope && l->hops < 256; l->hops += 1, l->i = 0) {   /* a file whose scopes go round is not followed for ever */
    const RdiScope *scope = rdi_at(&P->module->rdi, RDI_SCOPES, RdiScope, l->scope);
    RdiSpan locals = rdi_run(&P->module->rdi, RDI_LOCALS, scope->local_first, scope->local_count);
    if (l->i < locals.n) return (const RdiSymbol *)locals.p + l->i++;
    l->scope = scope->inline_site ? 0 : scope->parent;   /* an inlined call's variables end with it: what is outside is another function's */
  }
  return NULL;
}

const char *const eval_registers[18] = {"rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi", "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15", "rip", "rflags"};

/* The variable of a name at the frame: in its scopes, then a global or a constant, then a register. False: there is none;
   true with no kind: it has no place at this instruction. */
static bool name(Parse *P, RdiSpan id, EvalValue *v)
{
  const EngineModule *m = P->module;
  Locals l = {P->scope, 0, 0};
  for (const RdiSymbol *y; (y = local(P, &l));) {
    RdiSpan is = rdi_str(&m->rdi, y->name);
    if (same(id, is.p, is.n)) return *v = settle(P, place(P->e, m, y->type, y->location, P->voff, P->frame)), true;
  }
  for (uint32_t i = 0; i < 2 * (P->e->module_count + 1); i += 1) {   /* in each module: globals, then constants */
    const EngineModule *g = nth(P, i / 2);
    uint32_t idx = known(g, i % 2 ? RDI_NAMES_CONSTANTS : RDI_NAMES_GLOBALS, id);
    const RdiSymbol *y = idx ? rdi_at(&g->rdi, i % 2 ? RDI_CONSTANTS : RDI_GLOBALS, RdiSymbol, idx) : NULL;
    if (y) return *v = settle(P, place(P->e, g, y->type, y->location, 0, P->frame)), true;
  }
  for (uint32_t i = 0; i < 18; i += 1)   /* a register of the frame, as RAD has them: a name no variable has */
    if (same(id, eval_registers[i], strlen(eval_registers[i]))) return *v = i < 16 ? (EvalValue){EVAL_REGISTER, {NULL, U64, 0}, i} : value(U64, i == 16 ? P->frame->rip : P->frame->rflags), true;
  return false;
}

RdiSpan eval_members(EvalShape record)
{
  const Rdi *r = &record.to.module->rdi;
  const RdiUdt *u = rdi_at(r, RDI_UDTS, RdiUdt, record.to.idx);
  return rdi_run(r, RDI_MEMBERS, u->member_first, u->member_count);
}

/* The member of a name in a record. (Not built: members of what a C++ class is built on.) */
static EvalValue member(Parse *P, EvalValue v, RdiSpan id)
{
  EvalShape s = eval_shape(v.type);
  if (v.kind == EVAL_REGISTER || v.kind == EVAL_CONSTANT) {   /* a record of up to 8 bytes: its bytes, of which a member is some */
    uint64_t bits = 0;
    if (!P->dead && !eval_read(P->e, P->frame, v, &bits, 8)) return fail(P, "cannot be read");
    v.kind = EVAL_VALUE, v.at = bits;
  }
  if (v.kind && s.form == EVAL_RECORD) {
    RdiSpan members = eval_members(s);
    for (uint64_t i = 0; i < members.n; i += 1) {
      const RdiMember *b = (const RdiMember *)members.p + i;
      RdiSpan is = rdi_str(&v.type.module->rdi, b->name);
      if (b->kind == 1 && same(id, is.p, is.n)) return (EvalValue){v.kind, {v.type.module, b->type, 0}, v.kind == EVAL_MEMORY ? v.at + b->off : b->off < 8 ? v.at >> 8 * b->off : 0};
    }
  }
  return fail(P, "no such member");
}

/* A type's name at the text's position: C's words, or a name of the debug information that no variable here has; then its stars.
   False: there is none (and the position is anywhere). */
static bool type_name(Parse *P, EvalType *type)
{
  enum { NO = 0, INT_WORD = 0xfd, SIGNED, UNSIGNED };
  static const struct { const char *word; uint8_t kind; } words[] = {
    {"const", NO}, {"volatile", NO}, {"struct", NO}, {"union", NO}, {"enum", NO}, {"int", INT_WORD}, {"long", S32}, {"signed", SIGNED}, {"unsigned", UNSIGNED},
    {"void", VOID}, {"char", CHAR}, {"short", S16}, {"__int64", S64}, {"float", F32}, {"double", F64}, {"bool", BOOL}, {"_Bool", BOOL}};
  const char *before;
  uint32_t kind = 0;
  bool basic = false, named = false, sign = false, unsign = false;
  EvalValue variable;
  *type = (EvalType){0};
  for (;;) {
    before = P->p;
    RdiSpan id = word(P);
    uint32_t w = 0;
    while (w < sizeof words / sizeof *words && !same(id, words[w].word, strlen(words[w].word))) w += 1;
    if (w < sizeof words / sizeof *words && !named) {
      uint8_t k = words[w].kind;
      basic |= k != NO, sign |= k == SIGNED, unsign |= k == UNSIGNED;
      if (words[w].word[0] == 'l') kind = kind == S32 ? S64 : S32;   /* long, long long */
      else if (k != NO && k < INT_WORD) kind = k;   /* int beside short or long says nothing; alone it is what no word leaves */
    } else if (id.n && !basic && !named && !name(P, id, &variable)) {
      for (uint32_t i = 0; i <= P->e->module_count && !type->idx; i += 1) type->module = nth(P, i), type->idx = known(type->module, RDI_NAMES_TYPES, id);
      named = type->idx != 0;
      if (!named) break;
    } else {
      P->p = before;
      break;
    }
  }
  if (!basic && !named) return false;
  if (basic) {
    if (kind == 0) kind = S32;
    if (unsign) kind = kind == CHAR ? UCHAR : kind >= S16 && kind <= S64 ? kind - (S8 - 0xa) : kind;
    if (sign && kind == CHAR) kind = S8;
    *type = (EvalType){NULL, kind, 0};
  }
  for (;;) {
    if (eat(P, "*")) type->stars += 1;
    else if (!eat(P, "const")) return true;
  }
}

/* A number as C writes it, with C's type for it. */
static EvalValue literal(Parse *P)
{
  char *whole_end, *real_end;
  errno = 0;
  uint64_t v = strtoull(P->p, &whole_end, 0);
  double d = strtod(P->p, &real_end);
  if (real_end > whole_end) {
    P->p = real_end;
    if (*P->p != 'f' && *P->p != 'F') return number(d);
    return P->p += 1, convert(P, number(d), (EvalType){NULL, F32, 0});
  }
  if (errno == ERANGE) return fail(P, "a number too large");
  bool other = *P->p == '0', u = false, wide = false;   /* other: not decimal, and may be unsigned without saying so */
  for (P->p = whole_end; ; P->p += 1) {
    if ((*P->p | 0x20) == 'u') u = true;
    else if ((*P->p | 0x20) == 'l') wide |= (P->p[1] | 0x20) == 'l';
    else break;
  }
  wide |= v >> 32 || (!u && !other && v >> 31);
  u |= v >> 63 || (other && !wide && v >> 31);
  return value(wide ? (u ? U64 : S64) : u ? U32 : S32, v);
}

static EvalValue ternary(Parse *P);
static EvalValue unary(Parse *P);

static EvalValue primary(Parse *P)
{
  static const char escaped[] = "ntr0\\'", means[] = "\n\t\r\0\\'";
  EvalValue v = {0};
  EvalType type;
  if (eat(P, "(")) {
    v = ternary(P);
    if (!eat(P, ")")) return fail(P, "a ) is missing");
  } else if (*P->p == '\'') {   /* a character: an int, as in C */
    const char *c = P->p + 1, *e = *c == '\\' && c[1] ? strchr(escaped, c[1]) : NULL;
    if (*c == '\\' ? !e : !*c || *c == '\'') return fail(P, "not a character");
    v = value(S32, (uint64_t)(int64_t)(e ? means[e - escaped] : *c));
    c += e ? 2 : 1;
    if (*c != '\'') return fail(P, "a ' is missing");
    P->p = c + 1;
  } else if (isdigit((unsigned char)*P->p) || (*P->p == '.' && isdigit((unsigned char)P->p[1]))) v = literal(P);
  else {
    RdiSpan id = word(P);
    if (same(id, "sizeof", 6)) {
      const char *from = P->p;
      if (!(eat(P, "(") && type_name(P, &type) && eat(P, ")"))) {
        P->p = from, P->dead += 1;
        type = unary(P).type;
        P->dead -= 1;
      }
      return value(U64, eval_shape(type).size);
    }
    if (!id.n) return fail(P, "a value is missing");
    if (!name(P, id, &v)) return fail(P, "no such name");
    if (!v.kind) return fail(P, "it has no place here");
  }
  for (;;) {
    if (eat(P, "[")) {
      EvalValue i = load(P, ternary(P));
      if (!eat(P, "]")) return fail(P, "a ] is missing");
      v = deref(P, arith(P, '+', load(P, v), i));
    } else if (eat(P, ".") || eat(P, "->")) {   /* either, on a record or on an address of one, as RAD has it */
      RdiSpan id = word(P);
      int form = eval_shape(v.type).form;
      if (form == EVAL_ADDRESS || form == EVAL_ROW) v = deref(P, v);
      v = member(P, v, id);
    } else return v;
  }
}

static EvalValue unary(Parse *P)
{
  EvalValue v;
  EvalType type;
  if (P->deep == 64) return fail(P, "too deep");   /* every nesting comes through here: the stack it takes is bounded */
  P->deep += 1;
  eat(P, "");
  const char *from = P->p;
  if (eat(P, "(") && type_name(P, &type) && eat(P, ")")) v = convert(P, load(P, unary(P)), type);
  else if (P->p = from, eat(P, "*")) v = deref(P, unary(P));
  else if (eat(P, "&")) {
    v = unary(P);
    v = v.kind == EVAL_MEMORY ? (EvalValue){EVAL_VALUE, {v.type.module, v.type.idx, v.type.stars + 1}, v.at} : fail(P, "it has no address");
  }
  else if (eat(P, "-")) v = arith(P, '*', value(S32, (uint64_t)-1), load(P, unary(P)));   /* -1 * x: also -0.0 for 0.0 */
  else if (eat(P, "+")) v = arith(P, '*', value(S32, 1), load(P, unary(P)));
  else if (eat(P, "~")) v = arith(P, '^', value(S32, (uint64_t)-1), load(P, unary(P)));
  else if (eat(P, "!")) {
    v = load(P, unary(P));
    if (v.kind) v = value(S32, !nonzero(v, eval_shape(v.type)));
  } else v = primary(P);
  P->deep -= 1;
  return v;
}

/* Operators of two operands, of at most this level of C's (the lower binds tighter). */
static EvalValue binary(Parse *P, int most)
{
  static const struct { char text[3], op; uint8_t level; } ops[] = {   /* one that begins another comes after it */
    {"*", '*', 3}, {"/", '/', 3}, {"%", '%', 3}, {"+", '+', 4}, {"-", '-', 4}, {"<<", 'L', 5}, {">>", 'R', 5}, {"<=", 'l', 6}, {">=", 'g', 6}, {"<", '<', 6}, {">", '>', 6},
    {"==", 'e', 7}, {"!=", 'n', 7}, {"&&", 'A', 11}, {"||", 'O', 12}, {"&", '&', 8}, {"^", '^', 9}, {"|", '|', 10}};
  enum { OPS = sizeof ops / sizeof *ops };
  EvalValue a = unary(P), b;
  for (;;) {
    uint32_t i = 0;
    eat(P, "");
    const char *from = P->p;
    while (i < OPS && !eat(P, ops[i].text)) i += 1;
    if (i == OPS || ops[i].level > most) return P->p = from, a;
    a = load(P, a);
    if (ops[i].op == 'A' || ops[i].op == 'O') {
      bool is = ops[i].op == 'O', decided = a.kind && nonzero(a, eval_shape(a.type)) == is;
      P->dead += decided;
      b = load(P, binary(P, ops[i].level - 1));
      P->dead -= decided;
      a = a.kind && b.kind ? value(S32, decided ? is : nonzero(b, eval_shape(b.type))) : (EvalValue){0};
    } else {
      b = load(P, binary(P, ops[i].level - 1));
      a = arith(P, ops[i].op, a, b);
    }
  }
}

/* One side of a choice: read, when it is a number, if it is the side chosen. */
static EvalValue side(Parse *P, bool chosen, bool *is_number)
{
  P->dead += !chosen;
  EvalValue v = ternary(P);
  int form = eval_shape(v.type).form;
  *is_number = form == EVAL_INT || form == EVAL_REAL;
  if (*is_number) v = load(P, v);
  P->dead -= !chosen;
  return v;
}

static EvalValue ternary(Parse *P)
{
  EvalValue c = binary(P, 12), a, b;
  bool a_number, b_number;
  if (!eat(P, "?")) return c;
  c = load(P, c);
  bool yes = c.kind && nonzero(c, eval_shape(c.type));
  a = side(P, yes, &a_number);
  if (!eat(P, ":")) return fail(P, "a : is missing");
  b = side(P, !yes, &b_number);
  if (!a_number || !b_number) return yes ? a : b;
  return convert(P, yes ? a : b, (EvalType){NULL, common(eval_shape(a.type), eval_shape(b.type)), 0});
}

static Parse parse(const Engine *e, const EvalFrame *frame, const char *text)
{
  Parse P = {.e = e, .frame = &frame->regs, .module = engine_module(e, frame->regs.rip - frame->called), .p = text};
  P.voff = P.module ? frame->regs.rip - frame->called - P.module->image.base : 0;
  P.scope = P.module ? rdi_vmap(&P.module->rdi, RDI_SCOPE_VMAP, P.voff) : 0;
  for (uint32_t inside = frame->inlined, hops = 0; inside && P.scope && hops < 256; hops += 1) {   /* out of the inlined calls that are frames inside this one */
    const RdiScope *scope = rdi_at(&P.module->rdi, RDI_SCOPES, RdiScope, P.scope);
    inside -= scope->inline_site != 0, P.scope = scope->parent;
  }
  return P;
}

static EvalValue whole_text(Parse *P, EvalValue v, const char **error)
{
  eat(P, "");
  if (*P->p) fail(P, "what follows is not understood");
  *error = P->error;
  return P->error ? (EvalValue){0} : v;
}

EvalValue eval(const Engine *e, const EvalFrame *frame, const char *text, const char **error)
{
  Parse P = parse(e, frame, text);
  return whole_text(&P, ternary(&P), error);
}

EvalValue eval_as(const Engine *e, const EvalFrame *frame, const char *text, EvalType type, const char **error)
{
  Parse P = parse(e, frame, text);
  return whole_text(&P, convert(&P, load(&P, ternary(&P)), type), error);
}

EvalValue eval_load(const Engine *e, const EvalFrame *frame, EvalValue v, const char **error)
{
  Parse P = {.e = e, .frame = &frame->regs};
  v = load(&P, v);
  return *error = P.error, v;
}

uint32_t eval_locals(const Engine *e, const EvalFrame *frame, RdiSpan *names, uint32_t most)
{
  Parse P = parse(e, frame, NULL);
  Locals l = {P.scope, 0, 0};
  uint32_t n = 0;
  for (const RdiSymbol *y; n < most && (y = local(&P, &l));) {
    uint32_t i = 0;
    names[n] = rdi_str(&P.module->rdi, y->name);
    while (!same(names[n], names[i].p, names[i].n)) i += 1;   /* a name of an inner scope hides the same name further out */
    n += i == n;
  }
  return n;
}
