/* Places (dbg/eval.c) from locations written out by hand: every kind of location, the cases of a set, and the
   bytecode, operation by operation and broken in every way. Names and scopes need real debug information:
   they are checked on a running program (tests/zoo_check.c). */
#include "../dbg/eval.c"
#include <stdio.h>
#include <stdlib.h>

static uint8_t memory[64];   /* the target's, at MEMORY */
enum { MEMORY = 0x7000 };
bool target_read(Target *t, uint64_t addr, void *bytes, uint32_t n)
{
  (void)t;
  if (addr < MEMORY || addr - MEMORY > sizeof memory - n) return false;
  return memcpy(bytes, memory + (addr - MEMORY), n), true;
}
const EngineModule *engine_module(const Engine *e, uint64_t addr) { return (void)e, (void)addr, NULL; }

#define REG(code) 5, code, 8, 0, 0
#define U8(v) 11, v
#define U16(v) 12, (uint8_t)(v), (uint8_t)((v) >> 8)
#define U32(v) 13, (uint8_t)(v), (uint8_t)((v) >> 8), (uint8_t)((v) >> 16), (uint8_t)((v) >> 24)
#define U64(v) 14, (uint8_t)(v), (uint8_t)((v) >> 8), (uint8_t)((v) >> 16), (uint8_t)((v) >> 24), 0, 0, 0, 0
#define ADD 21, 0, 0
#define SIGNED(bits) 42, bits
#define READ(n) 4, n
#define CODE(...) (const uint8_t[]){__VA_ARGS__}, sizeof((const uint8_t[]){__VA_ARGS__})

static const TargetRegs regs = {.gp = {0x100, 0x200, 0x300, 0x400, MEMORY, 0x600, 0x700, 0x800, 0x900, 0xa00, 0xb00, 0xc00, 0xd00, 0xe00, 0xf00, 0x1000}};
static int failed;

/* The bytecode gives `want`; -1: it must be refused. */
static void code(const char *what, const uint8_t *bytes, size_t n, int64_t want)
{
  Engine e = {0};
  Rdi r = {0};
  uint64_t got = 0;
  uint8_t *copy = malloc(n);   /* exactly as long as it is, so that a read past its end is seen */
  memcpy(copy, bytes, n);
  r.s[RDI_LOCATION_BYTECODE] = (RdiSpan){copy, n};
  bool ok = bytecode(&e, &r, 0, &regs, &got);
  if (ok != (want != -1) || (ok && got != (uint64_t)want)) printf("test_eval: %s: %s %llx, not %llx\n", what, ok ? "gives" : "refused, with", (unsigned long long)got, (unsigned long long)want), failed = 1;
  free(copy);
}

static void at(const char *what, const Rdi *r, uint64_t location, uint64_t voff, int kind, uint64_t where)
{
  Engine e = {0};
  EngineModule m = {.image = {.base = 0x140000000}, .rdi = *r};
  EvalPlace p = place(&e, &m, 7, location, voff, &regs);
  if (p.kind != kind || (kind && (p.at != where || p.type != 7 || p.module != &m))) printf("test_eval: %s: kind %d at %llx, not %d at %llx\n", what, p.kind, (unsigned long long)p.at, kind, (unsigned long long)where), failed = 1;
}

int main(void)
{
  for (uint32_t i = 0; i < sizeof memory; i += 1) memory[i] = (uint8_t)(i + 1);
  /* as the converter writes a far place: register, offset as 32 bits with its sign, add */
  code("register + offset", CODE(REG(5), U32(40000), SIGNED(32), ADD, 0), MEMORY + 40000);
  code("register - offset", CODE(REG(6), U32(-40000), SIGNED(32), ADD, 0), 0x600 - 40000);
  code("the address stored there", CODE(REG(5), U32(8), SIGNED(32), ADD, READ(8), 0), 0x100f0e0d0c0b0a09);
  code("every register", CODE(REG(1), REG(16), ADD, 0), 0x1100);
  code("constants of each width", CODE(U8(0xff), U16(0xffff), ADD, U64(0x80000000), ADD, 0), 0x800100fe);
  code("sign from bit 8", CODE(U16(0x17f), SIGNED(8), 0), 0x7f);
  code("sign from bit 8, set", CODE(U16(0x180), SIGNED(8), 0), -128);
  code("no sign from bit 9", CODE(U16(0x0ff), SIGNED(9), 0), 0xff);
  code("all 64 bits kept", CODE(U64(0x12345678), SIGNED(64), 0), 0x12345678);
  code("a read of 2 bytes", CODE(U16(MEMORY + 2), READ(2), 0), 0x0403);
  code("a stack of 8", CODE(U8(1), U8(1), U8(1), U8(1), U8(1), U8(1), U8(1), U8(1), ADD, ADD, ADD, ADD, ADD, ADD, ADD, 0), 8);
  /* refused */
  code("a stack of 9", CODE(U8(1), U8(1), U8(1), U8(1), U8(1), U8(1), U8(1), U8(1), U8(1), 0), -1);
  code("nothing computed", CODE(0), -1);
  code("two values left", CODE(U8(1), U8(2), 0), -1);
  code("add with one value", CODE(U8(1), ADD, 0), -1);
  code("read with no address", CODE(READ(8), 0), -1);
  code("sign with no value", CODE(SIGNED(8), 0), -1);
  code("sign from bit 0", CODE(U8(1), SIGNED(0), 0), -1);
  code("sign from bit 65", CODE(U8(1), SIGNED(65), 0), -1);
  code("a read of 9 bytes", CODE(U16(MEMORY), READ(9), 0), -1);
  code("a read outside memory", CODE(U8(1), READ(8), 0), -1);
  code("register 0", CODE(REG(0), 0), -1);
  code("register 17", CODE(REG(17), 0), -1);
  code("half a register", CODE(5, 1, 4, 0, 0, 0), -1);
  code("a register from its second byte", CODE(5, 1, 8, 1, 0, 0), -1);
  code("an operation places do not use", CODE(1, 0), -1);   /* "no operation", which has no operand */
  code("an operation beyond the table", CODE(U8(1), 200, 0), -1);
  code("no end", CODE(U8(1)), -1);
  code("an operand cut short", CODE(13, 1, 0), -1);
  code("empty", (const uint8_t *)"", 0, -1);
  uint8_t longer[2 + 5 * 33 + 1] = {U8(0)};   /* 1 + 2 * 33 operations and the end: more than a place has */
  for (uint32_t i = 2; i < sizeof longer - 1; i += 5) memcpy(longer + i, (const uint8_t[]){U8(1), ADD}, 5);
  code("too many operations", longer, sizeof longer, -1);

  static const uint8_t far[] = {REG(5), U32(48), SIGNED(32), ADD, 0, REG(5), U32(8), SIGNED(32), ADD, READ(8), 0};
  static const RdiLocationCase cases[] = {{0x10, 0x20, 5ull << 56 | 3ull << 48}, {0x20, 0x30, 3ull << 56 | 5ull << 48 | 16ull << 32}, {0x30, 0x40, 1ull << 56 | 0}, {0x10, 0x50, 8ull << 56}};
  Rdi r = {0};
  r.s[RDI_LOCATION_BYTECODE] = (RdiSpan){far, sizeof far};
  r.s[RDI_LOCATION_SETS] = (RdiSpan){cases, 4};
  at("no location", &r, 0, 0, EVAL_NONE, 0);
  at("bytecode", &r, 1ull << 56, 0, EVAL_MEMORY, MEMORY + 48);
  at("bytecode that reads", &r, 1ull << 56 | 16, 0, EVAL_MEMORY, 0x100f0e0d0c0b0a09);
  at("register + offset", &r, 3ull << 56 | 5ull << 48 | 24ull << 32, 0, EVAL_MEMORY, MEMORY + 24);
  at("register - offset", &r, 3ull << 56 | 6ull << 48 | 0xfff8ull << 32, 0, EVAL_MEMORY, 0x600 - 8);
  at("register 17 + offset", &r, 3ull << 56 | 17ull << 48, 0, EVAL_NONE, 0);
  at("at the address stored there", &r, 4ull << 56 | 5ull << 48 | 16ull << 32, 0, EVAL_MEMORY, 0x1817161514131211);
  at("at an address stored outside memory", &r, 4ull << 56 | 6ull << 48, 0, EVAL_NONE, 0);
  at("in a register", &r, 5ull << 56 | 16ull << 48, 0, EVAL_REGISTER, 15);
  at("in register 0", &r, 5ull << 56, 0, EVAL_NONE, 0);
  at("in register 17", &r, 5ull << 56 | 17ull << 48, 0, EVAL_NONE, 0);
  at("in the image", &r, 6ull << 56 | 0x3000, 0, EVAL_MEMORY, 0x140003000);
  at("in the thread's block", &r, 7ull << 56 | 8, 0, EVAL_NONE, 0);
  at("a constant", &r, 8ull << 56 | 16, 0, EVAL_CONSTANT, 16);
  at("a set, before its cases", &r, 9ull << 56 | 3ull << 32, 0xf, EVAL_NONE, 0);
  at("a set, first case", &r, 9ull << 56 | 3ull << 32, 0x10, EVAL_REGISTER, 2);
  at("a set, end of the first case", &r, 9ull << 56 | 3ull << 32, 0x1f, EVAL_REGISTER, 2);
  at("a set, second case", &r, 9ull << 56 | 3ull << 32, 0x20, EVAL_MEMORY, MEMORY + 16);
  at("a set, third case", &r, 9ull << 56 | 3ull << 32, 0x3f, EVAL_MEMORY, MEMORY + 48);
  at("a set, after its cases", &r, 9ull << 56 | 3ull << 32, 0x40, EVAL_NONE, 0);
  at("a set of two, in the third's range", &r, 9ull << 56 | 2ull << 32, 0x30, EVAL_NONE, 0);
  at("a set from its second case", &r, 9ull << 56 | 2ull << 32 | 1, 0x30, EVAL_MEMORY, MEMORY + 48);
  at("a set beyond the table", &r, 9ull << 56 | 2ull << 32 | 4, 0, EVAL_NONE, 0);
  at("a set whose cases overlap: the first", &r, 9ull << 56 | 4ull << 32, 0x20, EVAL_MEMORY, MEMORY + 16);
  at("a set whose cases overlap: the only one left", &r, 9ull << 56 | 4ull << 32, 0x40, EVAL_CONSTANT, 0);

  /* reading: memory, a register's low bytes, a constant's */
  static const uint64_t constants[] = {0x1111111111111111, 0x8877665544332211};
  EngineModule m = {.rdi.s[RDI_LOCATION_CONSTANTS] = {constants, 2}};
  Engine e = {0};
  uint64_t got = 0;
  if (!eval_read(&e, &regs, (EvalPlace){EVAL_MEMORY, &m, 0, MEMORY + 4}, &got, 4) || got != 0x08070605) printf("test_eval: memory read %llx\n", (unsigned long long)got), failed = 1;
  if (!eval_read(&e, &regs, (EvalPlace){EVAL_REGISTER, &m, 0, 15}, &got, 2) || (uint16_t)got != 0x1000) printf("test_eval: register read %llx\n", (unsigned long long)got), failed = 1;
  if (!eval_read(&e, &regs, (EvalPlace){EVAL_CONSTANT, &m, 0, 8}, &got, 8) || got != 0x8877665544332211) printf("test_eval: constant read %llx\n", (unsigned long long)got), failed = 1;
  if (eval_read(&e, &regs, (EvalPlace){EVAL_CONSTANT, &m, 0, 16}, &got, 8)) printf("test_eval: a constant beyond the table was read\n"), failed = 1;
  if (eval_read(&e, &regs, (EvalPlace){EVAL_REGISTER, &m, 0, 1}, &got, 9)) printf("test_eval: 9 bytes of a register were read\n"), failed = 1;
  if (eval_read(&e, &regs, (EvalPlace){EVAL_NONE, &m, 0, 0}, &got, 1)) printf("test_eval: no place was read\n"), failed = 1;
  if (eval_read(&e, &regs, (EvalPlace){EVAL_MEMORY, &m, 0, 1}, &got, 1)) printf("test_eval: memory outside was read\n"), failed = 1;
  if (!failed) printf("test_eval: ok\n");
  return failed;
}
