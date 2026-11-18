/* A checksum record cut short at the very end of a module's line info: the block has no file,
   and nothing past the end is read (the buffer is exactly as long as the data, for ASan). */
#include "../dbg/pdb.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
  puts(ok ? "test_pdb ok" : "test_pdb FAILED");
  return !ok;
}
