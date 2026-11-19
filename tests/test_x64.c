/* The instruction decoder on instructions written out by hand: one of each kind stepping tells
   apart, the forms whose length depends on a prefix, and bytes that are no instruction or end
   early. The broad comparison is tests/oracle_x64.py. */
#include "../dbg/x64.h"
#include <stdio.h>
#include <string.h>

typedef struct { const char *name; uint8_t code[16]; uint32_t n; X64Inst want; } Case;
#define AT 0x140001000ull
static const Case cases[] = {
  {"call rel32", {0xe8, 0xad, 0xff, 0xff, 0xff}, 5, {5, X64_CALL, true, false, false, AT + 5 - 0x53}},
  {"call [rip+x]", {0xff, 0x15, 1, 2, 3, 4}, 6, {6, X64_CALL, false, false, false, 0}},
  {"call rax", {0xff, 0xd0}, 2, {2, X64_CALL, false, false, false, 0}},
  {"jmp rel8 back", {0xeb, 0xe9}, 2, {2, X64_JUMP, true, false, false, AT + 2 - 0x17}},
  {"jmp rel32", {0xe9, 0x00, 0x01, 0x00, 0x00}, 5, {5, X64_JUMP, true, false, false, AT + 5 + 0x100}},
  {"jmp [rax]", {0xff, 0x20}, 2, {2, X64_JUMP, false, false, false, 0}},
  {"je rel8", {0x74, 0x0b}, 2, {2, X64_BRANCH, true, false, false, AT + 2 + 0x0b}},
  {"jne rel32", {0x0f, 0x85, 0xf0, 0xff, 0xff, 0xff}, 6, {6, X64_BRANCH, true, false, false, AT + 6 - 0x10}},
  {"jrcxz", {0xe3, 0x02}, 2, {2, X64_BRANCH, true, false, false, AT + 4}},
  {"call rel32 under 66 (Intel reads 4 bytes, AMD 2)", {0x66, 0xe8, 0x00, 0x01, 0x00, 0x00}, 6, {6, X64_CALL, true, false, false, AT + 6 + 0x100}},
  {"jne rel32 under 66", {0x66, 0x0f, 0x85, 0x00, 0x01, 0x00, 0x00}, 7, {7, X64_BRANCH, true, false, false, AT + 7 + 0x100}},
  {"ret", {0xc3}, 1, {1, X64_RETURN, false, false, false, 0}},
  {"ret 8", {0xc2, 0x08, 0x00}, 3, {3, X64_RETURN, false, false, false, 0}},
  {"push rbx", {0x53}, 1, {1, X64_OTHER, false, true, false, 0}},
  {"pop r12", {0x41, 0x5c}, 2, {2, X64_OTHER, false, true, false, 0}},
  {"sub rsp, 0x20", {0x48, 0x83, 0xec, 0x20}, 4, {4, X64_OTHER, false, true, false, 0}},
  {"cmp rsp, 0x20", {0x48, 0x83, 0xfc, 0x20}, 4, {4, X64_OTHER, false, false, false, 0}},
  {"sub r12, 0x20", {0x49, 0x83, 0xec, 0x20}, 4, {4, X64_OTHER, false, false, false, 0}},
  {"lea rsp, [rbp-8]", {0x48, 0x8d, 0x65, 0xf8}, 4, {4, X64_OTHER, false, true, false, 0}},
  {"mov rsp, rbp", {0x48, 0x89, 0xec}, 3, {3, X64_OTHER, false, true, false, 0}},
  {"mov rbp, rsp", {0x48, 0x89, 0xe5}, 3, {3, X64_OTHER, false, false, false, 0}},
  {"mov ah, cl", {0x88, 0xcc}, 2, {2, X64_OTHER, false, false, false, 0}},
  {"mov spl, cl", {0x40, 0x88, 0xcc}, 3, {3, X64_OTHER, false, true, false, 0}},
  {"leave", {0xc9}, 1, {1, X64_OTHER, false, true, false, 0}},
  {"cmovne rsp, rax", {0x48, 0x0f, 0x45, 0xe0}, 4, {4, X64_OTHER, false, true, false, 0}},
  {"movups xmm4, xmm0", {0x0f, 0x10, 0xe0}, 3, {3, X64_OTHER, false, false, false, 0}},
  {"rep movsb", {0xf3, 0xa4}, 2, {2, X64_OTHER, false, false, true, 0}},
  {"rep ret", {0xf3, 0xc3}, 2, {2, X64_RETURN, false, false, false, 0}},
  {"mov rax, imm64", {0x48, 0xb8, 1, 2, 3, 4, 5, 6, 7, 8}, 10, {10, X64_OTHER, false, false, false, 0}},
  {"mov ax, imm16", {0x66, 0xb8, 0x34, 0x12}, 4, {4, X64_OTHER, false, false, false, 0}},
  {"mov eax, [abs64]", {0xa1, 1, 2, 3, 4, 5, 6, 7, 8}, 9, {9, X64_OTHER, false, false, false, 0}},
  {"test dword [rax], imm32", {0xf7, 0x00, 1, 2, 3, 4}, 6, {6, X64_OTHER, false, false, false, 0}},
  {"test cl, imm8 by its second encoding (f6 /1: processors run it, AMD's manual lists it, llvm-objdump does not read it)", {0xf6, 0xc9, 0x01}, 3, {3, X64_OTHER, false, false, false, 0}},
  {"not dword [rax]", {0xf7, 0x10}, 2, {2, X64_OTHER, false, false, false, 0}},
  {"mov [rsp+rax*8+0x100], imm32", {0xc7, 0x84, 0xc4, 0, 1, 0, 0, 1, 2, 3, 4}, 11, {11, X64_OTHER, false, false, false, 0}},
  {"mov eax, [rip+x]", {0x8b, 0x05, 1, 2, 3, 4}, 6, {6, X64_OTHER, false, false, false, 0}},
  {"vmovdqu ymm0, [rcx]", {0xc5, 0xfe, 0x6f, 0x01}, 4, {4, X64_OTHER, false, false, false, 0}},
  {"vpshufb (c4, map 2)", {0xc4, 0xe2, 0x79, 0x00, 0xc1}, 5, {5, X64_OTHER, false, false, false, 0}},
  {"vpalignr (map 3, imm8)", {0xc4, 0xe3, 0x79, 0x0f, 0xc1, 0x04}, 6, {6, X64_OTHER, false, false, false, 0}},
  {"vmovups zmm0, [rax] (EVEX)", {0x62, 0xf1, 0x7c, 0x48, 0x10, 0x00}, 6, {6, X64_OTHER, false, false, false, 0}},
  {"andn rsp, rax, rcx", {0xc4, 0xe2, 0x78, 0xf2, 0xe1}, 5, {5, X64_OTHER, false, true, false, 0}},
  {"blsr rsp, rax", {0xc4, 0xe2, 0x58, 0xf3, 0xc8}, 5, {5, X64_OTHER, false, true, false, 0}},
  {"nop dword [rax+rax+0]", {0x0f, 0x1f, 0x44, 0x00, 0x00}, 5, {5, X64_OTHER, false, false, false, 0}},
  {"int3", {0xcc}, 1, {1, X64_OTHER, false, false, false, 0}},
  {"call rel32 cut short", {0xe8, 0xad, 0xff}, 3, {0}},
  {"modrm missing", {0x8b}, 1, {0}},
  {"displacement cut short", {0x8b, 0x80, 1, 2}, 4, {0}},
  {"no opcode in 64-bit mode", {0x06}, 1, {0}},
  {"a map this does not read (EVEX map 5)", {0x62, 0xf5, 0x7c, 0x48, 0x58, 0xc1}, 6, {0}},
  {"prefixes only", {0x66, 0xf3}, 2, {0}},
  {"more than 15 bytes", {0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x90}, 16, {0}},
  {"exactly 15 bytes", {0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x90}, 15, {15, X64_OTHER, false, false, false, 0}},
  {"nothing", {0}, 0, {0}},
};

int main(void)
{
  int bad = 0;
  for (unsigned i = 0; i < sizeof cases / sizeof *cases; i += 1) {
    const Case *c = &cases[i];
    X64Inst x = x64_decode(c->code, c->n, AT);
    if (x.size != c->want.size || x.flow != c->want.flow || x.known != c->want.known || x.moves_sp != c->want.moves_sp || x.repeats != c->want.repeats || x.target != c->want.target)
      bad += 1, printf("x64 %s: size %u flow %u known %d sp %d rep %d target %llx\n", c->name, (unsigned)x.size, (unsigned)x.flow, (int)x.known, (int)x.moves_sp, (int)x.repeats, (unsigned long long)x.target);
  }
  return bad != 0;
}
