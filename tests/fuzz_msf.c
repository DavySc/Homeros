/* libFuzzer: any bytes as a PDB container; open it and read every stream. Property: no read
   outside the file, no allocation larger than the file. */
#include "../dbg/msf.h"
#include <stdlib.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
  uint8_t *buf = malloc(size ? size : 1);   /* exact size, so ASan sees one byte past the file */
  memcpy(buf, data, size);
  Msf m;
  if (msf_open(&m, buf, size)) {
    volatile uint32_t sum = 0;
    for (uint32_t i = 0; i < m.streams && i < 4096; i += 1) {
      uint32_t n;
      uint8_t *s = msf_stream(&m, i, &n);
      if (n) sum += s[0] + s[n - 1];
      free(s);
    }
    msf_close(&m);
  }
  free(buf);
  return 0;
}
