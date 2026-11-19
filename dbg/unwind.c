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

/* How many slots follow the unwind code c with its argument: an offset in units of 8, or (the "far" codes, and a large allocation of kind 1) in bytes. */
static uint32_t slots_after(const uint8_t *c)
{
  uint32_t op = c[1] & 15;
  return op == ALLOC_LARGE ? 1 + (c[1] >> 4) : op == SAVE_NONVOL || op == SAVE_XMM128 ? 1 : op == SAVE_NONVOL_FAR || op == SAVE_XMM128_FAR ? 2 : 0;
}

/* Whether rip stands in an epilogue, past the instruction that frees the frame's space (before that one the unwind
   codes still hold): (pop register)* then ret or a jump out of the function. If so, runs the pops. */
static bool epilogue(TargetRead *read, Target *t, TargetRegs *regs, uint64_t begin, uint64_t end)
{
  uint8_t c[32] = {0};
  uint32_t at = 0, got = sizeof c;
  while (got && !read(t, regs->rip, c, got)) got -= 1;   /* the code may end before 32 bytes do */
  TargetRegs r = *regs;
  for (;;) {
    uint32_t rex = (c[at] & 0xf0) == 0x40;
    if (at + rex >= 24 || (c[at + rex] & 0xf8) != 0x58) break;
    if (!pop(read, t, &r, &r.gp[(rex ? (c[at] & 1u) << 3 : 0) | (c[at + rex] & 7u)])) return false;
    at += rex + 1;
  }
  /* a jump ends a function when it leaves it or goes to its start, when it goes through the import table, or when the
     compiler marked it with a REX.W prefix it does not need (the mark that tells it from a switch) */
  uint64_t next = regs->rip + at, to = c[at] == 0xeb ? next + 2 + (uint64_t)(int8_t)c[at + 1] : next + 5 + (uint64_t)(int32_t)le32(c + at + 1);
  bool out = c[at] == 0xc3 || c[at] == 0xc2 || (c[at] == 0xf3 && c[at + 1] == 0xc3)
          || ((c[at] == 0xeb || c[at] == 0xe9) && (to < begin || to >= end || to == begin))
          || (c[at] == 0xff && c[at + 1] == 0x25) || ((c[at] & 0xf8) == 0x48 && c[at + 1] == 0xff && (c[at + 2] & 0x38) == 0x20);
  if (out && at < got) *regs = r;
  return out && at < got;
}

bool unwind_step(TargetRead *read, Target *t, const UnwindModule *m, TargetRegs *regs)
{
  const uint8_t *e = m && regs->rip >= m->base ? entry_at(m, regs->rip - m->base) : 0;
  uint8_t info[4 + 2 * 255 + 2 + 12], chain[12];
  bool first = true;
  uint64_t frame = 0;
  for (uint32_t depth = 0; e && depth < 32; depth += 1) {   /* the entry, then the entries it is chained to */
    uint64_t at = m->base + le32(e + 8), begin = m->base + le32(e), in = regs->rip - begin;
    if (!read(t, at, info, 4) || (info[0] & 7) < 1 || (info[0] & 7) > 2) return false;
    uint32_t count = info[2], frame_reg = info[3] & 15, flags = info[0] >> 3, slots = count + (count & 1);
    if (!read(t, at + 4, info + 4, 2 * slots + (flags & CHAINED ? 12 : 0))) return false;
    bool body = !first || in >= info[1];
    if (first && body && epilogue(read, t, regs, begin, m->base + le32(e + 4))) break;
    /* where saved registers are counted from, for the chained entries too: the first entry's frame register once the
       prologue has set it, else the stack pointer as it is now */
    uint64_t set = regs->gp[frame_reg] - 16 * (uint64_t)(info[3] >> 4);
    bool is_set = body || (flags & CHAINED);
    for (uint32_t i = 0; !is_set && i < count; i += 1 + slots_after(info + 4 + 2 * i)) is_set = (info[4 + 2 * i + 1] & 15) == SET_FPREG && in >= info[4 + 2 * i];
    if (first) frame = frame_reg && is_set ? set : regs->gp[RSP];
    for (uint32_t i = 0; i < count;) {
      const uint8_t *c = info + 4 + 2 * i;
      uint32_t op = c[1] & 15, x = c[1] >> 4, n = slots_after(c);
      if (i + 1 + n > count) return false;
      uint64_t arg = n == 2 ? le32(c + 2) : n == 1 ? le16(c + 2) : 0;
      i += 1 + n;
      if (!body && in < c[0]) continue;   /* the prologue has not come this far */
      if (op == PUSH_NONVOL) { if (!pop(read, t, regs, &regs->gp[x])) return false; }
      else if (op == ALLOC_LARGE) regs->gp[RSP] += n == 2 ? arg : 8 * arg;
      else if (op == ALLOC_SMALL) regs->gp[RSP] += 8 * x + 8;
      else if (op == SET_FPREG) regs->gp[RSP] = set;
      else if (op == SAVE_NONVOL || op == SAVE_NONVOL_FAR) { if (!read(t, frame + (n == 2 ? arg : 8 * arg), &regs->gp[x], 8)) return false; }
      else if (op == PUSH_MACHFRAME) {   /* an interrupt's frame: the registers at the interrupt, no return address */
        uint64_t sp = regs->gp[RSP] + 8 * x;
        return read(t, sp, &regs->rip, 8) && read(t, sp + 24, &regs->gp[RSP], 8);
      }
    }
    if (!(flags & CHAINED)) break;
    memcpy(chain, info + 4 + 2 * slots, 12), e = chain, first = false;
  }
  return pop(read, t, regs, &regs->rip);
}
