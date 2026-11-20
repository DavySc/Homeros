/* The unwinder on functions written out by hand, in memory of the test's own: each unwind code, a
   prologue stopped at every stage, chained entries, the epilogue forms of both versions of the unwind
   info, and tables that make no sense. Every stack slot holds its own number, so the registers after a step say which slots
   were read. The broad comparison, with the system's stack walker, is tests/stress_unwind.c. */
#include "../dbg/unwind.h"
#include "../dbg/le.h"
#include <stdio.h>
#include <stdlib.h>

enum { SLOTS = 16384, PUSH = 0, LARGE = 1, SMALL = 2, FPREG = 3, SAVE = 4, SAVE_FAR = 5, EPI = 6, SPARE = 7, XMM = 8, XMM_FAR = 9, MACH = 10, RBX = 3, RBP = 5, R12 = 12 };
#define BASE 0x140000000ull
#define STACK 0x200000ull
#define SP(i) (STACK + 8 * (uint64_t)(i))
#define SLOT(i) (0x7000000000ull + (uint64_t)(i))
#define OP(offset, op, x) offset, (op) | (x) << 4
#define WORD(v) (v) & 255, (v) >> 8
/* the function at 0x3000 to 0x3080 has `info`; the entry it may chain to, 0x3100 to 0x3200, has `parent` */
#define CHAIN 0x00, 0x31, 0, 0, 0x00, 0x32, 0, 0, 0x00, 0x21, 0, 0
#define PUSH_SUB 1, 5, 2, 0, OP(5, SMALL, 3), OP(1, PUSH, RBX)   /* push rbx; sub rsp, 0x20 */

static uint8_t image[0x4000];
static uint64_t stack[SLOTS];

static bool fake_read(Target *t, uint64_t addr, void *bytes, uint32_t n)
{
  (void)t;
  const uint8_t *from = addr >= BASE && addr - BASE <= sizeof image - n ? image + (addr - BASE)
                      : addr >= STACK && addr - STACK <= sizeof stack - n ? (const uint8_t *)stack + (addr - STACK) : 0;
  if (from) memcpy(bytes, from, n);
  return from;
}

typedef struct {
  const char *name;
  uint8_t info[24], parent[24];
  uint32_t in;            /* rip, from 0x3000 */
  uint8_t code[8];        /* the bytes at rip */
  uint32_t sp, rbp;       /* the slots rsp and rbp point at when the step begins */
  bool fail, mach;        /* the step must refuse; an interrupt's frame: rsp is read from the stack too */
  uint32_t ret;           /* the slot the return address must come from */
  uint32_t rbx, r12;      /* 1 + the slot the register must come from; 0: it must keep its value */
} Case;

static const Case cases[] = {
  {"outside every entry: only the return address", .in = 0x90, .ret = 0},
  {"between two entries, at the end of the first", {PUSH_SUB}, .in = 0x80, .ret = 0},
  {"an entry found at its first byte", .in = 0x300, .ret = 1},
  {"an entry found at its last byte", .in = 0x3ff, .ret = 1},
  {"body: push, small allocation", {PUSH_SUB}, .in = 0x10, .ret = 5, .rbx = 1 + 4},
  {"prologue, nothing run yet", {PUSH_SUB}, .in = 0, .ret = 0},
  {"prologue, the push has run", {PUSH_SUB}, .in = 1, .ret = 1, .rbx = 1 + 0},
  {"prologue, on its last byte", {PUSH_SUB}, .in = 4, .ret = 1, .rbx = 1 + 0},
  {"prologue done", {PUSH_SUB}, .in = 5, .ret = 5, .rbx = 1 + 4},
  {"large allocation in units of 8", {1, 7, 2, 0, OP(7, LARGE, 0), WORD(9)}, .in = 0x10, .ret = 9},
  {"large allocation in bytes, over 64 KB", {1, 7, 3, 0, OP(7, LARGE, 1), WORD(0x10), WORD(1)}, .in = 0x10, .ret = 0x10010 / 8},
  {"save at an offset in units of 8", {1, 9, 3, 0, OP(9, SAVE, RBX), WORD(3), OP(4, SMALL, 4)}, .in = 0x10, .ret = 5, .rbx = 1 + 3},
  {"save at an offset in bytes", {1, 9, 4, 0, OP(9, SAVE_FAR, RBX), WORD(0x18), WORD(0), OP(4, SMALL, 4)}, .in = 0x10, .ret = 5, .rbx = 1 + 3},
  {"save at an offset in bytes, over 64 KB", {1, 9, 4, 0, OP(9, SAVE_FAR, RBX), WORD(0x10), WORD(1), OP(4, SMALL, 4)}, .in = 0x10, .ret = 5, .rbx = 1 + 0x10010 / 8},
  {"the arguments of vector saves are passed over", {1, 9, 6, 0, OP(9, XMM, 6), WORD(0x0302), OP(8, XMM_FAR, 7), WORD(0x0302), WORD(0x0302), OP(1, PUSH, RBX)}, .in = 0x10, .ret = 1, .rbx = 1 + 0},
  /* push rbp; sub rsp, 0x30; mov [rsp+0x10], rbx; lea rbp, [rsp+0x20] */
#define FRAMED 1, 0x14, 5, RBP | 2 << 4, OP(0xf, FPREG, 0), OP(0xa, SAVE, RBX), WORD(2), OP(5, SMALL, 5), OP(1, PUSH, RBP)
  {"frame register: the stack pointer is not used in the body", {FRAMED}, .in = 0x20, .sp = 40, .rbp = 14, .ret = 21, .rbx = 1 + 16},
  {"frame register, prologue before it is set: saves count from rsp", {FRAMED}, .in = 0xc, .rbp = 30, .ret = 7, .rbx = 1 + 2},
  {"frame register, prologue just after it is set", {FRAMED}, .in = 0xf, .sp = 40, .rbp = 14, .ret = 21, .rbx = 1 + 16},
  {"chained: the parent's codes run in full", {1 | 4 << 3, 0, 0, 0, CHAIN}, {PUSH_SUB}, .in = 0x10, .ret = 5, .rbx = 1 + 4},
  {"chained after an odd number of codes", {1 | 4 << 3, 0, 1, 0, OP(0, SMALL, 0), 0, 0, CHAIN}, {PUSH_SUB}, .in = 0, .ret = 6, .rbx = 1 + 5},
  {"chained: the first entry's frame register holds for the parent's saves, whatever its prologue size",
   {1 | 4 << 3, 0x20, 0, RBP | 2 << 4, CHAIN}, {1, 0, 2, 0, OP(0, SAVE, RBX), WORD(2)}, .in = 0, .sp = 3, .rbp = 14, .ret = 3, .rbx = 1 + 16},
  {"an interrupt's frame", {1, 0, 1, 0, OP(0, MACH, 0)}, .in = 0, .mach = true, .ret = 0},
  {"an interrupt's frame with an error code", {1, 0, 1, 0, OP(0, MACH, 1)}, .in = 0, .mach = true, .ret = 1},
  {"epilogue: pop, ret", {PUSH_SUB}, .in = 0x40, {0x5b, 0xc3}, .ret = 1, .rbx = 1 + 0},
  {"epilogue: ret", {PUSH_SUB}, .in = 0x40, {0xc3}, .ret = 0},
  {"epilogue: ret n", {PUSH_SUB}, .in = 0x40, {0xc2, 8, 0}, .ret = 0},
  {"epilogue: rep ret", {PUSH_SUB}, .in = 0x40, {0xf3, 0xc3}, .ret = 0},
  {"epilogue: pop r12, pop rbx, ret", {PUSH_SUB}, .in = 0x40, {0x41, 0x5c, 0x5b, 0xc3}, .ret = 2, .rbx = 1 + 1, .r12 = 1 + 0},
  {"no epilogue: a pop, then something else", {PUSH_SUB}, .in = 0x40, {0x5b, 0x90, 0xc3}, .ret = 5, .rbx = 1 + 4},
  {"epilogue: looked for in the prologue too", {PUSH_SUB}, .in = 1, {0xc3}, .ret = 0},
  {"epilogue: add rsp with a byte, pop, ret", {PUSH_SUB}, .in = 0x40, {0x48, 0x83, 0xc4, 0x18, 0x5b, 0xc3}, .ret = 4, .rbx = 1 + 3},
  {"epilogue: add rsp with a negative byte", {PUSH_SUB}, .in = 0x40, {0x48, 0x83, 0xc4, 0xf8, 0xc3}, .sp = 2, .ret = 1},
  {"epilogue: add rsp with four bytes", {PUSH_SUB}, .in = 0x40, {0x48, 0x81, 0xc4, 0x18, 0, 0, 0, 0xc3}, .ret = 3},
  {"epilogue: lea rsp from the frame register, offset in a byte", {FRAMED}, .in = 0x40, {0x48, 0x8d, 0x65, 0x08, 0xc3}, .sp = 40, .rbp = 14, .ret = 19},
  {"epilogue: lea rsp from the frame register, offset in four bytes", {FRAMED}, .in = 0x40, {0x48, 0x8d, 0xa5, 0x08, 0, 0, 0, 0xc3}, .sp = 40, .rbp = 14, .ret = 19},
  {"no epilogue: lea rsp from a register that is not the frame's", {FRAMED}, .in = 0x40, {0x48, 0x8d, 0x63, 0x08, 0xc3}, .sp = 40, .rbp = 14, .ret = 21, .rbx = 1 + 16},
  {"no epilogue: lea rsp where there is no frame register", {PUSH_SUB}, .in = 0x40, {0x48, 0x8d, 0x60, 0x08, 0xc3}, .ret = 5, .rbx = 1 + 4},
  {"epilogue: lea rsp from r13 as the frame register", {1, 0, 0, 13 | 2 << 4}, .in = 0x40, {0x49, 0x8d, 0x65, 0x08, 0xc3}, .sp = 40, .rbp = 14, .ret = 19},
  {"epilogue: a prefix before the ret", {PUSH_SUB}, .in = 0x40, {0xf2, 0xc3}, .ret = 0},
  {"epilogue: short jump out of the function", {PUSH_SUB}, .in = 0x40, {0xeb, 0x7f}, .ret = 0},
  {"epilogue: pop, jump to the function's end", {PUSH_SUB}, .in = 0x40, {0x5b, 0xe9, 0x3a, 0, 0, 0}, .ret = 1, .rbx = 1 + 0},
  {"no epilogue: short jump back inside", {PUSH_SUB}, .in = 0x70, {0xeb, 0xf0}, .ret = 5, .rbx = 1 + 4},
  {"no epilogue: short jump inside", {PUSH_SUB}, .in = 0x40, {0xeb, 0x02}, .ret = 5, .rbx = 1 + 4},
  {"epilogue: jump to before the function", {PUSH_SUB}, .in = 0x40, {0xe9, 0xb0, 0xff, 0xff, 0xff}, .ret = 0},
  {"epilogue: jump to the function's own start", {PUSH_SUB}, .in = 0x40, {0xe9, 0xbb, 0xff, 0xff, 0xff}, .ret = 0},
  {"no epilogue: jump back inside", {PUSH_SUB}, .in = 0x40, {0xe9, 0xbc, 0xff, 0xff, 0xff}, .ret = 5, .rbx = 1 + 4},
  {"epilogue: jump to the function's end", {PUSH_SUB}, .in = 0x40, {0xe9, 0x3b, 0, 0, 0}, .ret = 0},
  {"epilogue: jump through the import table", {PUSH_SUB}, .in = 0x40, {0xff, 0x25, 0, 0, 0, 0}, .ret = 0},
  {"epilogue: marked jump through a register", {PUSH_SUB}, .in = 0x40, {0x48, 0xff, 0xe0}, .ret = 0},
  {"no epilogue: unmarked jump through a register (a switch)", {PUSH_SUB}, .in = 0x40, {0xff, 0xe0}, .ret = 5, .rbx = 1 + 4},
  {"no epilogue: marked call through a register", {PUSH_SUB}, .in = 0x40, {0x48, 0xff, 0xd0}, .ret = 5, .rbx = 1 + 4},
  {"no epilogue: jump into another part of the same function", {1 | 4 << 3, 0, 0, 0, CHAIN}, {PUSH_SUB}, .in = 0x40, {0xe9, 0xcb, 0, 0, 0}, .ret = 5, .rbx = 1 + 4},
  {"epilogue: jump to the start of the function this is a part of", {1 | 4 << 3, 0, 0, 0, CHAIN}, {PUSH_SUB}, .in = 0x40, {0xe9, 0xbb, 0, 0, 0}, .ret = 0},
  {"epilogue: jump from a part into another function", {1 | 4 << 3, 0, 0, 0, CHAIN}, {PUSH_SUB}, .in = 0x40, {0xe9, 0xcb, 0x02, 0, 0}, .ret = 0},
  {"epilogue: jump into the middle of another function", {PUSH_SUB}, .in = 0x40, {0xe9, 0xcb, 0, 0, 0}, .ret = 0},
  {"no epilogue: a part's jump to its own start", {1 | 4 << 3, 0, 0, 0, CHAIN}, {PUSH_SUB}, .in = 0x40, {0xe9, 0xbb, 0xff, 0xff, 0xff}, .ret = 5, .rbx = 1 + 4},
  {"an epilogue code and its slot are passed over", {1, 5, 4, 0, OP(0, EPI, 0), OP(0, PUSH, R12), OP(5, SMALL, 3), OP(1, PUSH, RBX)}, .in = 0x10, .ret = 5, .rbx = 1 + 4},
  {"a spare code and its two slots are passed over", {1, 5, 5, 0, OP(0, SPARE, 0), OP(0, PUSH, R12), OP(0, PUSH, R12), OP(5, SMALL, 3), OP(1, PUSH, RBX)}, .in = 0x10, .ret = 5, .rbx = 1 + 4},
  /* version 2: push rbx; sub rsp, 0x20 ... add rsp, 0x20; then at the very end: pop rbx; ret */
#define V2 2, 5, 4, 0, OP(2, EPI, 1), OP(0, EPI, 0), OP(5, SMALL, 3), OP(1, PUSH, RBX)
  {"version 2: body", {V2}, .in = 0x10, .ret = 5, .rbx = 1 + 4},
  {"version 2: prologue, the push has run", {V2}, .in = 1, .ret = 1, .rbx = 1 + 0},
  {"version 2: the instructions are not read", {V2}, .in = 0x40, {0xc3}, .ret = 5, .rbx = 1 + 4},
  {"version 2: just before the epilogue", {V2}, .in = 0x7d, .ret = 5, .rbx = 1 + 4},
  {"version 2: at the epilogue's start", {V2}, .in = 0x7e, .ret = 1, .rbx = 1 + 0},
  {"version 2: in the epilogue, the pop has run", {V2}, .in = 0x7f, .ret = 0},
#define V2_LIST 2, 5, 6, 0, OP(2, EPI, 0), OP(0x40, EPI, 0), OP(0x20, EPI, 0), OP(0, EPI, 0), OP(5, SMALL, 3), OP(1, PUSH, RBX)
  {"version 2: an epilogue at the end is not assumed", {V2_LIST}, .in = 0x7e, .ret = 5, .rbx = 1 + 4},
  {"version 2: the first listed epilogue", {V2_LIST}, .in = 0x40, .ret = 1, .rbx = 1 + 0},
  {"version 2: past the first listed epilogue", {V2_LIST}, .in = 0x42, .ret = 5, .rbx = 1 + 4},
  {"version 2: in the second listed epilogue", {V2_LIST}, .in = 0x61, .ret = 0},
  {"version 2: an epilogue more than 255 bytes before the end", .parent = {2, 2, 3, 0, OP(1, EPI, 0), OP(0, EPI, 1), OP(1, PUSH, RBX)}, .in = 0x100, .ret = 1, .rbx = 1 + 0},
  /* push rbx; push r12 ... pop r12; pop rbx; ret */
#define V2_TWO 2, 3, 4, 0, OP(4, EPI, 1), OP(0, EPI, 0), OP(3, PUSH, R12), OP(1, PUSH, RBX)
  {"version 2: two pops to run", {V2_TWO}, .in = 0x7c, .ret = 2, .rbx = 1 + 1, .r12 = 1 + 0},
  {"version 2: a pop of r12 is two bytes long", {V2_TWO}, .in = 0x7e, .ret = 1, .rbx = 1 + 0},
  {"version 2: both pops have run", {V2_TWO}, .in = 0x7f, .ret = 0},
  /* push rcx (kept by nobody: 8 bytes of frame); push rbx ... pop rbx; pop rcx; ret */
#define V2_SCRATCH 2, 2, 4, 0, OP(3, EPI, 1), OP(0, EPI, 0), OP(2, PUSH, RBX), OP(1, SMALL, 0)
  {"version 2: a pop of a register nobody keeps", {V2_SCRATCH}, .in = 0x7d, .ret = 2, .rbx = 1 + 0},
  {"version 2: that pop still to run", {V2_SCRATCH}, .in = 0x7e, .ret = 1},
  {"version 2: that pop has run", {V2_SCRATCH}, .in = 0x7f, .ret = 0},
  {"version 2: no epilogue codes, no epilogue", {2, 5, 2, 0, OP(5, SMALL, 3), OP(1, PUSH, RBX)}, .in = 0x7c, .ret = 5, .rbx = 1 + 4},
  {"version 2: a code's argument is not taken for a push", {2, 9, 5, 0, OP(2, EPI, 1), OP(0, EPI, 0), OP(8, LARGE, 0), OP(0, PUSH, R12), OP(1, PUSH, RBX)}, .in = 0x7e, .ret = 1, .rbx = 1 + 0},
  {"version 2: an interrupt's frame under a pop nobody keeps", {2, 2, 5, 0, OP(3, EPI, 1), OP(0, EPI, 0), OP(2, PUSH, RBX), OP(1, SMALL, 0), OP(0, MACH, 0)}, .in = 0x7d, .mach = true, .ret = 2, .rbx = 1 + 0},
  {"version 2: an epilogue without pops", {2, 4, 3, 0, OP(1, EPI, 1), OP(0, EPI, 0), OP(4, SMALL, 3)}, .in = 0x7f, .ret = 0},
  {"version 2: an epilogue that ends an interrupt", {2, 0, 3, 0, OP(1, EPI, 1), OP(0, EPI, 0), OP(0, MACH, 0)}, .in = 0x7f, .mach = true, .ret = 0},
  {"version 2: the pushes are in the entry chained to", {2 | 4 << 3, 0, 2, 0, OP(2, EPI, 1), OP(0, EPI, 0), CHAIN}, {PUSH_SUB}, .in = 0x7e, .ret = 1, .rbx = 1 + 0},
  {"a code that does not exist", {1, 0, 1, 0, OP(0, 11, 0)}, .in = 0x10, .fail = true},
  {"a code whose argument lies past the codes", {1, 7, 1, 0, OP(7, LARGE, 0)}, .in = 0x10, .fail = true},
  {"a return address that cannot be read", {PUSH_SUB}, .in = 0x10, .sp = SLOTS - 3, .fail = true},
};

static void put32(uint32_t at, uint32_t v) { memcpy(image + at, &v, 4); }

int main(void)
{
  /* the image: headers, an exception table of three entries at 0x1000, their unwind info from 0x2000 */
  memcpy(image, "MZ", 2), put32(0x3c, 0x80), memcpy(image + 0x80, "PE\0\0", 4), image[0x80 + 24] = 0x0b, image[0x80 + 25] = 2;
  put32(0x80 + 24 + 56, 0x123000), put32(0x80 + 24 + 112 + 24, 0x1000), put32(0x80 + 24 + 112 + 28, 36);
  static const uint32_t table[9] = {0x3000, 0x3080, 0x2000, 0x3100, 0x3200, 0x2100, 0x3300, 0x3400, 0x2200};
  memcpy(image + 0x1000, table, sizeof table);
  memcpy(image + 0x2200, (uint8_t[]){1, 0, 1, 0, OP(0, SMALL, 0)}, 6);   /* the third: 8 bytes of frame from its first byte on */
  for (uint32_t i = 0; i < SLOTS; i += 1) stack[i] = SLOT(i);
  UnwindModule m, none;
  if (!unwind_module(fake_read, 0, BASE, &m) || m.base != BASE || m.size != 0x123000 || m.count != 3) return printf("test_unwind: the module's header is misread\n"), 1;
  image[0x80 + 24] = 0x0c;
  if (unwind_module(fake_read, 0, BASE, &none) || unwind_module(fake_read, 0, BASE + 0x1000, &none)) return printf("test_unwind: a module where none is\n"), 1;
  uint32_t failed = 0;
  for (uint32_t i = 0; i < sizeof cases / sizeof *cases; i += 1) {
    const Case *c = &cases[i];
    memcpy(image + 0x2000, c->info, 24), memcpy(image + 0x2100, c->parent, 24);
    memset(image + 0x3000, 0, 0x400), memcpy(image + 0x3000 + c->in, c->code, 8);
    for (int with = 1; with >= (c->info[0] || c->in >= 0x100 ? 1 : 0); with -= 1) {   /* what has no entry unwinds the same without the module */
      TargetRegs r = {.rip = BASE + 0x3000 + c->in, .gp = {[0] = SP(50), [RBX] = 0xb, [4] = SP(c->sp), [RBP] = SP(c->rbp) + 0x20, [R12] = 0xc, [13] = SP(c->rbp) + 0x20}};
      bool ok = unwind_step(fake_read, 0, with ? &m : 0, &r);
      if (ok == c->fail || (ok && (r.rip != SLOT(c->ret) || r.gp[4] != (c->mach ? SLOT(c->ret + 3) : SP(c->ret + 1))
                                   || r.gp[RBX] != (c->rbx ? SLOT(c->rbx - 1) : 0xb) || r.gp[R12] != (c->r12 ? SLOT(c->r12 - 1) : 0xc))))
        failed += 1, printf("test_unwind: %s: ok %d, return from slot %lld, rsp %llx, rbx %llx, r12 %llx\n", c->name, ok, (long long)(r.rip - SLOT(0)),
                            (unsigned long long)r.gp[4], (unsigned long long)r.gp[RBX], (unsigned long long)r.gp[R12]);
    }
  }
  free(m.table);
  if (failed) return 1;
  printf("test_unwind: %zu cases ok\n", sizeof cases / sizeof *cases);
  return 0;
}
