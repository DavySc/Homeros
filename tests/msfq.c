/* msfq <file.pdb> <dir>: writes every stream to <dir>/<index> and prints "index size" lines,
   for tests/oracle_msf.sh to compare with llvm-pdbutil. */
#include "../dbg/msf.h"
#include "../dbg/load.h"

int main(int argc, char **argv)
{
  uint64_t size;
  void *buf = argc == 3 ? load(argv[1], &size) : 0;
  Msf m;
  if (!buf || !msf_open(&m, buf, size)) return 2;
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
  return 0;
}
