/* Answers lookup queries from stdin against an RDI file, one answer line per query, so
   tests/oracle_rdi.py can compare the reader with RAD's own dump of the same file.
   v <section> <voff> | l <voff> | s <file> <line> | n <kind> <hex name> | t <string idx> |
   p <procedure idx> | c <scope idx> */
#include "../dbg/rdi.h"
#include "load.h"

int main(int argc, char **argv)
{
  uint64_t size;
  void *buf = argc == 2 ? load(argv[1], &size) : 0;
  Rdi r;
  if (!buf || !rdi_open(&r, buf, size)) return 2;
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
    } else if (op == 'p') {   /* a procedure: root scope, then its name */
      const RdiSymbol *y = rdi_at(&r, RDI_PROCEDURES, RdiSymbol, a);
      s = rdi_str(&r, y->name);
      printf("%u ", y->root_scope);
      fwrite(s.p, 1, s.n, stdout);
    } else if (op == 'c') {   /* a scope: procedure, child, sibling, then its offset ranges */
      const RdiScope *c = rdi_at(&r, RDI_SCOPES, RdiScope, a);
      printf("%u %u %u", c->proc, c->first_child, c->next_sibling);
      s = rdi_run(&r, RDI_SCOPE_VOFFS, c->voff_first, c->voff_opl - c->voff_first);
      for (uint64_t i = 0; i < s.n; i += 1) printf(" %llu", (unsigned long long)((const uint64_t *)s.p)[i]);
    } else if (op == 't') { s = rdi_str(&r, (uint32_t)a); fwrite(s.p, 1, s.n, stdout); }
    putchar('\n');
  }
  free(buf);
  return 0;
}
