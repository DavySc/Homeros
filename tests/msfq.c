/* msfq <file.pdb> <dir>: writes every stream to <dir>/<index> and prints "index size" lines,
   for tests/oracle_msf.sh to compare with llvm-pdbutil. */
#include "../dbg/msf.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
  FILE *f = argc == 3 ? fopen(argv[1], "rb") : 0;
  if (!f) return 2;
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  rewind(f);
  uint8_t *buf = malloc((size_t)size + 1);
  Msf m;
  if (fread(buf, 1, (size_t)size, f) != (size_t)size || !msf_open(&m, buf, (uint64_t)size)) return 2;
  for (uint32_t i = 0; i < m.streams; i += 1) {
    char path[4096];
    uint32_t n;
    uint8_t *s = msf_stream(&m, i, &n);
    snprintf(path, sizeof path, "%s/%u", argv[2], i);
    FILE *o = fopen(path, "wb");
    if (!o || (n && fwrite(s, 1, n, o) != n) || fclose(o) != 0) return 2;
    printf("%u %u\n", i, n);
    free(s);
  }
  msf_close(&m);
  free(buf);
  fclose(f);
  return 0;
}
