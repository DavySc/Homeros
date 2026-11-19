/* libFuzzer: any bytes as an instruction. Properties: no read outside the bytes given; a size
   within them and at most 15; the instruction's own bytes alone read the same; one byte fewer is
   no instruction. */
#include "../dbg/x64.h"
#include <stdlib.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
  uint8_t *buf = malloc(size ? size : 1);   /* exact size, so ASan sees one byte past the end */
  memcpy(buf, data, size);
  X64Inst x = x64_decode(buf, (uint32_t)size, 0x1000);
  if (x.size > size || x.size > 15) abort();
  if (x.size) {
    X64Inst same = x64_decode(buf, x.size, 0x1000), cut = x64_decode(buf, x.size - 1u, 0x1000);
    if (same.size != x.size || same.flow != x.flow || same.known != x.known || same.target != x.target || same.moves_sp != x.moves_sp || same.repeats != x.repeats || cut.size) abort();
  } else if (x.flow || x.known || x.target || x.moves_sp || x.repeats) abort();
  free(buf);
  return 0;
}
