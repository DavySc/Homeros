/* Answers lookup queries from stdin against an RDI file, one answer line per query, so
   tests/oracle_rdi.py can compare the reader with RAD's own dump of the same file.
   v <section> <voff> | l <voff> | s <file> <line> | n <kind> <hex name> | t <string idx> */
#include "../dbg/rdi.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
  FILE *f = argc == 2 ? fopen(argv[1], "rb") : 0;
  if (!f) return 2;
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  rewind(f);
  uint64_t *buf = malloc((size_t)size + 8);
  Rdi r;
  if (fread(buf, 1, (size_t)size, f) != (size_t)size || !rdi_open(&r, buf, (uint64_t)size)) return 2;
  static char hex[1 << 16];
  char op;
  unsigned long long a, b;
  while (scanf(" %c %llu", &op, &a) == 2) {
    RdiSpan s = {0, 0};
    if (op == 'v' && scanf("%llu", &b) == 1) printf("%u", rdi_vmap(&r, (int)a, b));
    else if (op == 'l') { RdiLine l = rdi_line_from_voff(&r, a); printf("%u %u", l.file, l.line); }
    else if (op == 's' && scanf("%llu", &b) == 1) {
      s = rdi_voffs_from_line(&r, (uint32_t)a, (uint32_t)b);
      for (uint64_t i = 0; i < s.n; i += 1) printf(" %llu", (unsigned long long)((const uint64_t *)s.p)[i]);
    } else if (op == 'n' && scanf("%65535s", hex) == 1) {
      uint64_t len = 0;
      for (; hex[1 + 2 * len]; len += 1) { unsigned c; sscanf(hex + 1 + 2 * len, "%2x", &c); hex[len] = (char)c; }
      s = rdi_matches(&r, (uint32_t)a, hex, len);
      for (uint64_t i = 0; i < s.n; i += 1) printf(" %u", ((const uint32_t *)s.p)[i]);
    } else if (op == 't') { s = rdi_str(&r, (uint32_t)a); fwrite(s.p, 1, s.n, stdout); }
    putchar('\n');
  }
  free(buf);
  return 0;
}
