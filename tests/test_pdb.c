/* Line info by hand. A checksum record cut short at the very end of a module's line info: the block has no file,
   and nothing past the end is read (the buffer is exactly as long as the data, for ASan). */
#include "../dbg/pdb.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* pdb_inlinee on the first n bytes, in a block of exactly n */
static bool inlinee(const uint8_t *bytes, uint32_t n, uint32_t id, uint32_t *sum_at, uint32_t *line)
{
  uint8_t *heap = memcpy(malloc(n), bytes, n);
  bool found = pdb_inlinee((PdbSpan){heap, n}, id, sum_at, line);
  free(heap);
  return found;
}

int main(void)
{
  static const uint8_t c13[] = {
    0xf2, 0, 0, 0, 32, 0, 0, 0,                   /* a lines subsection of 32 bytes */
    0, 0, 0, 0, 1, 0, 0, 0, 16, 0, 0, 0,          /* offset 0 of section 1, no columns, 16 bytes of code */
    0, 0, 0, 0, 1, 0, 0, 0, 20, 0, 0, 0,          /* a file: its checksum is at 0, 1 line, 20 bytes */
    0, 0, 0, 0, 7, 0, 0, 0,                       /* offset 0 is line 7 */
    0xf4, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0};       /* checksums: 4 bytes, where a record needs 6 */
  uint8_t *heap = malloc(sizeof c13), section[40] = {0};
  memcpy(heap, c13, sizeof c13);
  section[13] = 0x10;                             /* the section is at 0x1000 */
  Pdb p = {.sec = section, .sections = 1};
  PdbSpan lines = {heap, sizeof c13};
  PdbLineBlock b;
  PdbLineCursor c = {0};
  int ok = pdb_line_block(&p, lines, &c, &b) && b.count == 1 && b.base == 0x1000 && b.opl == 0x1010 && b.file.n == 0 && b.sum.n == 0
           && !pdb_line_block(&p, lines, &c, &b);
  free(heap);
  /* Where inlined functions are written, in the form with more files per function: function 7 is
     listed after one with 2 more files; function 9 claims more files than there are bytes. */
  static const uint8_t inlinees[] = {
    0xf6, 0, 0, 0, 60, 0, 0, 0, 1, 0, 0, 0,       /* 60 bytes, signature 1 */
    5, 0, 0, 0, 24, 0, 0, 0, 30, 0, 0, 0, 2, 0, 0, 0, 1, 0, 0, 0, 2, 0, 0, 0,
    7, 0, 0, 0, 48, 0, 0, 0, 70, 0, 0, 0, 0, 0, 0, 0,
    9, 0, 0, 0, 72, 0, 0, 0, 90, 0, 0, 0, 0xff, 0xff, 0xff, 0x3f};
  uint32_t sum_at = 0, line = 0;
  ok = ok && inlinee(inlinees, sizeof inlinees, 7, &sum_at, &line) && sum_at == 48 && line == 70 && inlinee(inlinees, sizeof inlinees, 9, &sum_at, &line) && line == 90
       && !inlinee(inlinees, sizeof inlinees, 2, &sum_at, &line) && !inlinee(inlinees, sizeof inlinees, 1, &sum_at, &line)
       && !inlinee(inlinees, sizeof inlinees - 4, 9, &sum_at, &line) && !inlinee(inlinees, 11, 5, &sum_at, &line);   /* cut inside the last entry; inside the signature */
  puts(ok ? "test_pdb ok" : "test_pdb FAILED");
  return !ok;
}
