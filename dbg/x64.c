#include "x64.h"

/* What follows an opcode: a ModRM byte, an immediate of 1 byte, of 2, of 2 or 4 by the operand
   size (Z), and marks for the bytes that are no opcode. */
enum { M = 1, I1 = 2, I2 = 4, Z = 8, PREFIX = 16, BAD = 32 };
static const uint8_t one_byte[256] = {
  /* 00 */ M, M, M, M, I1, Z, BAD, BAD, M, M, M, M, I1, Z, BAD, BAD,           /* 0f: the two-byte map, read apart */
  /* 10 */ M, M, M, M, I1, Z, BAD, BAD, M, M, M, M, I1, Z, BAD, BAD,
  /* 20 */ M, M, M, M, I1, Z, PREFIX, BAD, M, M, M, M, I1, Z, PREFIX, BAD,
  /* 30 */ M, M, M, M, I1, Z, PREFIX, BAD, M, M, M, M, I1, Z, PREFIX, BAD,
  /* 40 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,                     /* REX, read apart */
  /* 50 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  /* 60 */ BAD, BAD, BAD, M, PREFIX, PREFIX, PREFIX, PREFIX, Z, M | Z, I1, M | I1, 0, 0, 0, 0,   /* 62: EVEX, read apart */
  /* 70 */ I1, I1, I1, I1, I1, I1, I1, I1, I1, I1, I1, I1, I1, I1, I1, I1,
  /* 80 */ M | I1, M | Z, BAD, M | I1, M, M, M, M, M, M, M, M, M, M, M, M,
  /* 90 */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, BAD, 0, 0, 0, 0, 0,
  /* a0 */ 0, 0, 0, 0, 0, 0, 0, 0, I1, Z, 0, 0, 0, 0, 0, 0,                    /* a0 to a3: an address, read apart */
  /* b0 */ I1, I1, I1, I1, I1, I1, I1, I1, 0, 0, 0, 0, 0, 0, 0, 0,             /* b8 to bf: read apart */
  /* c0 */ M | I1, M | I1, I2, 0, BAD, BAD, M | I1, M | Z, I1 | I2, 0, I2, 0, 0, I1, BAD, 0,   /* c4, c5: VEX, read apart */
  /* d0 */ M, M, M, M, BAD, BAD, BAD, 0, M, M, M, M, M, M, M, M,
  /* e0 */ I1, I1, I1, I1, I1, I1, I1, I1, Z, Z, BAD, I1, 0, 0, 0, 0,
  /* f0 */ PREFIX, 0, PREFIX, PREFIX, 0, 0, M, M, 0, 0, 0, 0, 0, 0, M, M};
/* the two-byte map (0f xx): all have a ModRM byte but these; a few take one more byte */
static const uint8_t two_byte_bare[] = {0x05, 0x06, 0x07, 0x08, 0x09, 0x0b, 0x0e, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x37, 0x77, 0xa0, 0xa1, 0xa2, 0xa8, 0xa9, 0xaa};
static const uint8_t two_byte_imm[] = {0x0f, 0x70, 0x71, 0x72, 0x73, 0xa4, 0xac, 0xba, 0xc2, 0xc4, 0xc5, 0xc6};
/* and those of it that write a general register: the one ModRM's reg names, the one its r/m names */
static const uint8_t two_byte_to_reg[] = {0x02, 0x03, 0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f,
                                          0x50, 0xaf, 0xb2, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xbc, 0xbd, 0xbe, 0xbf, 0xc0, 0xc1, 0xc5, 0xd7};
static const uint8_t two_byte_to_rm[] = {0x20, 0x21, 0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f,
                                         0xa4, 0xa5, 0xab, 0xac, 0xad, 0xb0, 0xb1, 0xb3, 0xbb, 0xc0, 0xc1};

static bool among(const uint8_t *set, unsigned n, uint8_t op)
{
  for (unsigned i = 0; i < n; i += 1) if (set[i] == op) return true;
  return false;
}

X64Inst x64_decode(const uint8_t *code, uint32_t n, uint64_t addr)
{
  X64Inst none = {0}, x = {0};
  uint32_t at = 0, map = 0;   /* map: 0 one byte, 1 after 0f, 2 after 0f 38, 3 after 0f 3a */
  bool small = false, short_addr = false, wide = false, rex = false, rex_b = false, rex_r = false, third_sp = false;
  uint8_t vex = 0, rep = 0;   /* the byte that began VEX or EVEX; the last of f2 and f3 */
#define BYTE(v) do { if (at >= n) return none; (v) = code[at++]; } while (0)
  uint8_t op, b;
  for (;;) {   /* prefixes */
    BYTE(op);
    if (!(one_byte[op] & PREFIX)) break;
    small = small || op == 0x66, short_addr = short_addr || op == 0x67, rep = op == 0xf2 || op == 0xf3 ? op : rep;
  }
  if (op >= 0x40 && op <= 0x4f) { rex = true, wide = op & 8, rex_r = op & 4, rex_b = op & 1; BYTE(op); }
  uint8_t flags;
  if (op == 0xc4 || op == 0xc5 || op == 0x62) {   /* VEX and EVEX: the REX bits upside down, the map and a third register, then one opcode byte */
    uint8_t v;
    vex = op;
    BYTE(b);
    rex_r = !(b & 0x80), v = b;
    if (op == 0xc5) map = 1;
    else { map = b & (op == 0xc4 ? 31 : 7), rex_b = !(b & 0x20); BYTE(v); }
    if (op == 0x62) BYTE(b);
    if (map < 1 || map > 3) return none;
    third_sp = (v >> 3 & 15) == 11, small = (v & 3) == 1, rep = (v & 3) == 2 ? 0xf3 : (v & 3) == 3 ? 0xf2 : 0;
    BYTE(op);
  } else if (op == 0x0f) {
    map = 1;
    BYTE(op);
    if (op == 0x38 || op == 0x3a) { map = op == 0x38 ? 2 : 3; BYTE(op); }
  }
  if (map == 0) flags = one_byte[op];
  else if (map == 1) flags = op >= 0x80 && op <= 0x8f ? Z : op >= 0xc8 && op <= 0xcf ? 0 : among(two_byte_bare, sizeof two_byte_bare, op) ? 0 : among(two_byte_imm, sizeof two_byte_imm, op) ? M | I1 : M;
  else flags = map == 3 ? M | I1 : M;
  if (flags & BAD) return none;
  uint8_t modrm = 0, mod = 0, reg = 0, rm = 0;
  if (flags & M) {
    BYTE(modrm);
    mod = modrm >> 6, reg = modrm >> 3 & 7, rm = modrm & 7;
    uint8_t base = rm;
    if (mod != 3 && rm == 4) { BYTE(b); base = b & 7; }                 /* SIB */
    uint32_t disp = mod == 1 ? 1 : mod == 2 || (mod == 0 && base == 5) ? 4 : 0;
    at += disp;
  }
  /* the immediate; the jumps and calls among them are relative to the next instruction */
  uint32_t imm = (flags & I1 ? 1 : 0) + (flags & I2 ? 2 : 0) + (flags & Z ? (small && !wide && !(map == 0 && (op == 0xe8 || op == 0xe9)) && !(map == 1) ? 2 : 4) : 0);
  if (map == 0 && op >= 0xb8 && op <= 0xbf) imm = wide ? 8 : small ? 2 : 4;          /* mov register, immediate */
  if (map == 0 && op >= 0xa0 && op <= 0xa3) imm = short_addr ? 4 : 8;                /* mov to or from an absolute address */
  if (map == 0 && (op == 0xf6 || op == 0xf7) && reg < 2) imm = op == 0xf6 ? 1 : small && !wide ? 2 : 4;   /* test */
  bool amd = map == 1 && !vex && op == 0x78 && mod == 3 && (small || rep == 0xf2);                         /* extrq, insertq; else 78 is vmread */
  if (amd) imm = 2;
  if (at + imm > n || at + imm > 15) return none;   /* the one place that looks at the end: nothing after it reads before it has */
  int64_t rel = imm == 1 ? (int8_t)code[at] : imm == 4 ? (int32_t)((uint32_t)code[at] | (uint32_t)code[at + 1] << 8 | (uint32_t)code[at + 2] << 16 | (uint32_t)code[at + 3] << 24) : 0;
  at += imm;
  x.size = (uint8_t)at, x.target = addr + at + (uint64_t)rel;
  /* register 4 is the stack pointer, but as a byte without REX it is ah; in the one-byte map the even opcodes are the byte forms
     but for 8c (mov from a segment register), in the two-byte map setcc and the byte cmpxchg and xadd */
  bool sp = rex || vex || (map == 0 ? (op & 1) || op == 0x8c : map != 1 || !((op >= 0x90 && op <= 0x9f) || op == 0xb0 || op == 0xc0));
  bool sp_rm = sp && mod == 3 && rm == 4 && !rex_b, sp_reg = sp && reg == 4 && !rex_r;
  if (map == 0) {
    bool grp = op == 0xff;
    x.flow = op == 0xe8 || (grp && (reg == 2 || reg == 3)) ? X64_CALL : op == 0xe9 || op == 0xeb || (grp && (reg == 4 || reg == 5)) ? X64_JUMP
           : (op >= 0x70 && op <= 0x7f) || (op >= 0xe0 && op <= 0xe3) ? X64_BRANCH : op == 0xc2 || op == 0xc3 || op == 0xca || op == 0xcb || op == 0xcf ? X64_RETURN : X64_OTHER;
    x.known = x.flow != X64_OTHER && x.flow != X64_RETURN && !grp;
    x.repeats = rep && ((op >= 0xa4 && op <= 0xa7) || (op >= 0xaa && op <= 0xaf) || (op >= 0x6c && op <= 0x6f));
    /* push, pop, pushf, popf, enter, leave; xchg with rax, mov of an immediate; then the instructions whose
       destination is the ModRM's r/m, then those where it is its reg (cmp and test write nothing) */
    x.moves_sp = (op >= 0x50 && op <= 0x5f) || op == 0x68 || op == 0x6a || op == 0x9c || op == 0x9d || op == 0xc8 || op == 0xc9 || (grp && reg == 6) || op == 0x8f
              || ((op == 0x94 || op == 0xbc || (op == 0xb4 && rex)) && !rex_b)
              || (sp_rm && ((op < 0x38 && (op & 6) == 0) || ((op == 0x80 || op == 0x81 || op == 0x83) && reg != 7) || (op >= 0x86 && op <= 0x89) || op == 0x8c
                            || op == 0xc6 || op == 0xc7 || ((op == 0xfe || grp) && reg < 2) || ((op == 0xf6 || op == 0xf7) && (reg == 2 || reg == 3))
                            || op == 0xc0 || op == 0xc1 || (op >= 0xd0 && op <= 0xd3)))
              || (sp_reg && ((op < 0x38 && (op & 6) == 2) || op == 0x63 || op == 0x69 || op == 0x6b || op == 0x86 || op == 0x87 || op == 0x8a || op == 0x8b || op == 0x8d));
  } else if (map == 1 && !vex) {
    x.flow = op >= 0x80 && op <= 0x8f ? X64_BRANCH : X64_OTHER, x.known = x.flow == X64_BRANCH;
    /* push and pop of fs and gs, bswap; then by ModRM. By prefix or by the reg field an opcode is several instructions:
       2c, 2d convert to a general register under f2 and f3 only; 7e is movd to r/m but under f3; 78 is vmread or AMD's;
       sldt, str, smsw, the bit tests that write, rdrand, rdseed; under f3 rdfsbase, rdgsbase, rdssp */
    x.moves_sp = op == 0xa0 || op == 0xa1 || op == 0xa8 || op == 0xa9 || (op == 0xcc && !rex_b)
              || (sp_reg && (among(two_byte_to_reg, sizeof two_byte_to_reg, op) || (rep && (op == 0x2c || op == 0x2d))))
              || (sp_rm && (among(two_byte_to_rm, sizeof two_byte_to_rm, op) || (op == 0x7e && rep != 0xf3) || (op == 0x78 && !amd)
                            || (op == 0x00 && reg < 2) || (op == 0x01 && reg == 4) || (op == 0xba && reg > 4) || (op == 0xc7 && reg > 5)
                            || (rep == 0xf3 && ((op == 0xae && reg < 2) || (op == 0x1e && reg == 1)))));
  } else if (map == 1)   /* with VEX all are vector or mask instructions but the moves and conversions out of them */
    x.moves_sp = (sp_reg && (op == 0x50 || op == 0x93 || op == 0xc5 || op == 0xd7 || (rep && (op == 0x2c || op == 0x2d || op == 0x78 || op == 0x79))))
              || (sp_rm && op == 0x7e && rep != 0xf3);
  else if (map == 2)   /* movbe, crc32, adcx, adox, encodekey, urdmsr; with VEX the bit instructions, of which f3 writes the third register and mulx it too */
    x.moves_sp = vex ? (sp_reg && (op == 0xf2 || op == 0xf5 || op == 0xf6 || op == 0xf7)) || (third_sp && (op == 0xf3 || op == 0xf6))
                     : sp_reg && (op == 0xf0 || (op == 0xf1 && rep == 0xf2) || (op == 0xf6 && (small || rep == 0xf3)) || ((op == 0xfa || op == 0xfb) && rep == 0xf3)
                                  || (op == 0xf8 && rep == 0xf2 && mod == 3));
  else   /* the extracts into r/m; rorx */
    x.moves_sp = (sp_rm && op >= 0x14 && op <= 0x17) || (sp_reg && op == 0xf0);
  if (!x.known) x.target = 0;
  return x;
}
