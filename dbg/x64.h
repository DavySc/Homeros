/* One x86-64 instruction, as far as stepping needs it (the knowledge base, 03: trap nets): how
   long it is, whether it is a call, a jump, a conditional branch or a return, where it goes
   when the bytes say so, and whether it writes the stack pointer. Not a disassembler. */
#ifndef DBG_X64_H
#define DBG_X64_H
#include <stdbool.h>
#include <stdint.h>

enum { X64_OTHER, X64_CALL, X64_JUMP, X64_BRANCH, X64_RETURN };
typedef struct {
  uint8_t size;       /* 1 to 15; 0 when the bytes are no instruction or end inside one */
  uint8_t flow;
  bool known;         /* target is where the call, jump or branch goes (else a register or memory decides) */
  bool moves_sp;      /* writes the stack pointer, a call and a return not counted (it may be true for one that
                         only reads it; false for one that writes it would be a bug) */
  bool repeats;       /* a string instruction under a repeat prefix */
  uint64_t target;
} X64Inst;

/* The instruction in the n bytes at code, which stand at address addr. */
X64Inst x64_decode(const uint8_t *code, uint32_t n, uint64_t addr);
#endif
