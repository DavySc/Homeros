/* Prints what dbg/x64.c reads from each line "address hex bytes" of the input (for tests/oracle_x64.py). */
#include "../dbg/x64.h"
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
  char line[256];
  while (fgets(line, sizeof line, stdin)) {
    char *p;
    uint64_t addr = strtoull(line, &p, 16);
    uint8_t code[32];
    uint32_t n = 0;
    for (char *q; n < sizeof code; p = q, n += 1) {
      unsigned long v = strtoul(p, &q, 16);
      if (q == p) break;
      code[n] = (uint8_t)v;
    }
    X64Inst x = x64_decode(code, n, addr);
    printf("%llx %u %u %d %llx %d %d\n", (unsigned long long)addr, (unsigned)x.size, (unsigned)x.flow, (int)x.known, (unsigned long long)x.target, (int)x.moves_sp, (int)x.repeats);
  }
  return 0;
}
