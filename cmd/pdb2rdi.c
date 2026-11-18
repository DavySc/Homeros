/* pdb2rdi <in.pdb> <out.rdi> */
#include "../dbg/conv.h"
#include "../tests/load.h"

int main(int argc, char **argv)
{
  uint64_t size, n;
  void *pdb = argc == 3 ? load(argv[1], &size) : 0;
  uint8_t *rdi = pdb ? rdi_from_pdb(pdb, size, &n) : 0;
  FILE *out = rdi ? fopen(argv[2], "wb") : 0;
  if (!out || fwrite(rdi, 1, n, out) != n || fclose(out) != 0) {
    fprintf(stderr, "pdb2rdi: cannot convert %s\n", argc > 1 ? argv[1] : "(usage: pdb2rdi in.pdb out.rdi)");
    return 1;
  }
  free(rdi);
  free(pdb);
  return 0;
}
