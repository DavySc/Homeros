#include "unwind.h"
#include "le.h"
#include <stdlib.h>

enum { RSP = 4, CHAINED = 4 };
enum { PUSH_NONVOL, ALLOC_LARGE, ALLOC_SMALL, SET_FPREG, SAVE_NONVOL, SAVE_NONVOL_FAR, EPILOG, SPARE, SAVE_XMM128, SAVE_XMM128_FAR, PUSH_MACHFRAME };

static bool pop(TargetRead *read, Target *t, TargetRegs *r, uint64_t *into)
{
  bool ok = read(t, r->gp[RSP], into, 8);
  r->gp[RSP] += 8;
  return ok;
}

bool unwind_module(TargetRead *read, Target *t, uint64_t base, UnwindModule *m)
{
  uint8_t dos[0x40], pe[24 + 144];   /* signature, file header, and the optional header as far as the exception table's entry */
  *m = (UnwindModule){.base = base};
  if (!read(t, base, dos, sizeof dos) || le16(dos) != 0x5a4d || !read(t, base + le32(dos + 0x3c), pe, sizeof pe) || le32(pe) != 0x4550 || le16(pe + 24) != 0x20b) return false;
  m->size = le32(pe + 24 + 56), m->count = le32(pe + 24 + 112 + 3 * 8 + 4) / 12;
  if (!m->count) return true;
  if (!(m->table = malloc(12 * (size_t)m->count))) abort();
  if (read(t, base + le32(pe + 24 + 112 + 3 * 8), m->table, 12 * m->count)) return true;
  free(m->table), m->table = 0, m->count = 0;
  return false;
}

/* The table entry (begin, end, unwind info; offsets from the module's base) that holds off: the last that begins at or before it. */
static const uint8_t *entry_at(const UnwindModule *m, uint64_t off)
{
  uint32_t lo = 0, hi = m->count;
  while (lo < hi) {
    uint32_t mid = lo + (hi - lo) / 2;
    if (le32(m->table + 12 * mid) <= off) lo = mid + 1;
    else hi = mid;
  }
  return lo && off < le32(m->table + 12 * (lo - 1) + 4) ? m->table + 12 * (lo - 1) : 0;
}

/* How many slots follow the unwind code c (the system's table: an epilogue code counts as a pair). */
static uint32_t slots_after(const uint8_t *c)
{
  static const uint8_t extra[16] = {[ALLOC_LARGE] = 1, [SAVE_NONVOL] = 1, [SAVE_NONVOL_FAR] = 2, [EPILOG] = 1, [SPARE] = 2, [SAVE_XMM128] = 1, [SAVE_XMM128_FAR] = 2};
  return extra[c[1] & 15] + ((c[1] & 15) == ALLOC_LARGE && c[1] >> 4);
}

/* Where the function begins that the entry e is a part of: the entry at the end of e's chain (LookupPrimaryFunctionEntry). */
static uint32_t primary(TargetRead *read, Target *t, const UnwindModule *m, const uint8_t *e)
{
  uint8_t info[4], chain[12];
  for (uint32_t depth = 0; depth < 32; depth += 1, e = chain) {
    uint64_t at = m->base + le32(e + 8);
    if (!read(t, at, info, 4) || !(info[0] >> 3 & CHAINED) || !read(t, at + 4 + 2 * (info[2] + (info[2] & 1u)), chain, 12)) break;
  }
  return le32(e);
}

/* An interrupt's frame at sp: the registers at the interrupt, no return address. */
static bool machine_frame(TargetRead *read, Target *t, TargetRegs *regs, uint64_t sp)
{
  return read(t, sp, &regs->rip, 8) && read(t, sp + 24, &regs->gp[RSP], 8);
}

/* Unwind info of version 1 does not describe epilogues, so the instructions at rip are read: an epilogue is
   (add rsp, n | lea rsp, [frame register + n])? (pop register)* then ret or a jump that leaves the function.
   If rip stands in one, runs it up to the ret. e: the entry that holds rip, with these flags and frame register. */
static bool epilogue(TargetRead *read, Target *t, const UnwindModule *m, const uint8_t *e, uint32_t flags, uint32_t frame_reg, TargetRegs *regs)
{
  uint8_t c[32] = {0};
  uint32_t at = 0, got = sizeof c;
  while (got && !read(t, regs->rip, c, got)) got -= 1;   /* the code may end before 32 bytes do */
  TargetRegs r = *regs;
  bool wide = c[1] == 0x81 || (c[2] & 0xf8) == 0xa0;
  uint64_t n = wide ? (uint64_t)(int32_t)le32(c + 3) : (uint64_t)(int8_t)c[3];
  uint32_t fp = (c[0] & 1u) << 3 | (c[2] & 7u);
  if (c[0] == 0x48 && (c[1] == 0x83 || c[1] == 0x81) && c[2] == 0xc4) r.gp[RSP] += n, at = wide ? 7 : 4;
  else if ((c[0] & 0xfe) == 0x48 && c[1] == 0x8d && fp && fp == frame_reg && ((c[2] & 0xf8) == 0x60 || wide)) r.gp[RSP] = r.gp[fp] + n, at = wide ? 7 : 4;
  for (;;) {
    uint32_t rex = (c[at] & 0xf0) == 0x40;
    if (at + rex >= 24 || (c[at + rex] & 0xf8) != 0x58) break;
    if (!pop(read, t, &r, &r.gp[(rex ? (c[at] & 1u) << 3 : 0) | (c[at + rex] & 7u)])) return false;
    at += rex + 1;
  }
  at += c[at] == 0xf2;   /* a prefix compilers put before a ret or jmp; it changes nothing here */
  /* a jump ends the function when it goes to its start, or out of this entry to what is not another part of the same
     function (SameFunction); when it goes through the import table; or when the compiler marked it with a REX.W
     prefix it does not need (the mark that tells it from a switch) */
  uint64_t next = regs->rip - m->base + at, to = c[at] == 0xeb ? next + 2 + (uint64_t)(int8_t)c[at + 1] : next + 5 + (uint64_t)(int32_t)le32(c + at + 1);
  bool out = c[at] == 0xc3 || c[at] == 0xc2 || (c[at] == 0xf3 && c[at + 1] == 0xc3)
          || (c[at] == 0xff && c[at + 1] == 0x25) || ((c[at] & 0xf8) == 0x48 && c[at + 1] == 0xff && (c[at + 2] & 0x38) == 0x20);
  if (!out && (c[at] == 0xeb || c[at] == 0xe9)) {
    const uint8_t *target = entry_at(m, to);
    uint32_t begin = primary(read, t, m, e);
    out = to < le32(e) || to >= le32(e + 4) ? !target || primary(read, t, m, target) != begin || to == begin : to == le32(e) && !(flags & CHAINED);
  }
  if (out && at < got) *regs = r;
  return out && at < got;
}

bool unwind_step(TargetRead *read, Target *t, const UnwindModule *m, TargetRegs *regs)
{
  const uint8_t *e = m && regs->rip >= m->base ? entry_at(m, regs->rip - m->base) : 0;
  uint8_t info[4 + 2 * 255 + 2 + 12], chain[12];
  uint64_t frame = 0;
  int64_t epi = -1;   /* how far rip stands in an epilogue that the unwind codes describe (version 2) */
  bool machine = false;
  for (uint32_t depth = 0; e && depth < 32; depth += 1) {   /* the entry, then the entries it is chained to */
    uint64_t at = m->base + le32(e + 8);
    uint32_t in = (uint32_t)(regs->rip - m->base) - le32(e);
    if (!read(t, at, info, 4)) return false;
    uint32_t count = info[2], frame_reg = info[3] & 15, flags = info[0] >> 3, slots = count + (count & 1);
    if (!read(t, at + 4, info + 4, 2 * slots + (flags & CHAINED ? 12 : 0))) return false;
    if (!depth) {
      /* where saved registers are counted from, for the chained entries too: the frame register once the prologue
         has set it, else the stack pointer as it is now */
      bool is_set = in >= info[1] || (flags & CHAINED);
      for (uint32_t i = 0; !is_set && i < count; i += 1 + slots_after(info + 4 + 2 * i)) is_set = (info[4 + 2 * i + 1] & 15) == SET_FPREG && in >= info[4 + 2 * i];
      frame = frame_reg && is_set ? regs->gp[frame_reg] - 16 * (uint64_t)(info[3] >> 4) : regs->gp[RSP];
      if ((info[0] & 7) < 2) { if (epilogue(read, t, m, e, flags, frame_reg, regs)) break; }
      else   /* version 2: the first codes say how long an epilogue is and how far before the entry's end each begins */
        for (uint32_t i = 0, size = info[4]; i < count && (info[5 + 2 * i] & 15) == EPILOG; i += 1) {
          uint32_t off = i ? info[4 + 2 * i] + 256u * (info[5 + 2 * i] >> 4) : size * (info[5] >> 4 & 1u);   /* the first: at the very end, if marked so */
          uint32_t past = in + le32(e) - (le32(e + 4) - off);
          if (past < size) epi = past;
        }
    }
    uint32_t i = 0;
    if (epi >= 0) {   /* the epilogue is the pushes undone, one byte of code each (two for r8 to r15): those rip has not yet passed */
      while (i < count && (info[5 + 2 * i] & 15) != PUSH_NONVOL && (info[5 + 2 * i] & 15) != PUSH_MACHFRAME) i += 1 + slots_after(info + 4 + 2 * i);
      if (i < count) {   /* else they are in the entry this one is chained to, or there are none */
        uint32_t done = 0;
        for (; i < count && (info[5 + 2 * i] & 15) == PUSH_NONVOL; done += 1 + (info[5 + 2 * i] >> 7), i += 1)
          if (done >= epi && !pop(read, t, regs, &regs->gp[info[5 + 2 * i] >> 4])) return false;
        if (i < count && info[5 + 2 * i] == ALLOC_SMALL) regs->gp[RSP] += done >= epi ? 8 : 0, i += 1;   /* a push of a register that need not be kept */
        if (i < count && (info[5 + 2 * i] & 15) == PUSH_MACHFRAME) return machine_frame(read, t, regs, regs->gp[RSP]);
        break;
      }
    }
    while (i < count) {
      const uint8_t *c = info + 4 + 2 * i;
      uint32_t op = c[1] & 15, x = c[1] >> 4, n = slots_after(c);
      if (i + 1 + n > count || op > PUSH_MACHFRAME) return false;
      uint64_t arg = n == 2 ? le32(c + 2) : n == 1 ? le16(c + 2) : 0;
      i += 1 + n;
      if (in < c[0]) continue;   /* the prologue has not come this far */
      if (op == PUSH_NONVOL) { if (!pop(read, t, regs, &regs->gp[x])) return false; }
      else if (op == ALLOC_LARGE) regs->gp[RSP] += n == 2 ? arg : 8 * arg;
      else if (op == ALLOC_SMALL) regs->gp[RSP] += 8 * x + 8;
      else if (op == SET_FPREG) regs->gp[RSP] = regs->gp[frame_reg] - 16 * (uint64_t)(info[3] >> 4);
      else if (op == SAVE_NONVOL || op == SAVE_NONVOL_FAR) { if (!read(t, frame + (n == 2 ? arg : 8 * arg), &regs->gp[x], 8)) return false; }
      else if (op == PUSH_MACHFRAME) { if (!machine_frame(read, t, regs, regs->gp[RSP] + (x ? 8 : 0))) return false; machine = true; }
    }
    if (!(flags & CHAINED)) break;
    memcpy(chain, info + 4 + 2 * slots, 12), e = chain;
  }
  return machine || pop(read, t, regs, &regs->rip);
}
