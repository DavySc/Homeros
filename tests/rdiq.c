/* Answers lookup queries from stdin against an RDI file, one answer line per query, so
   tests/oracle_rdi.py can compare the reader with RAD's own dump of the same file.
   v <section> <voff> | l <voff> | s <file> <line> | n <kind> <hex name> | t <string idx> |
   p <procedure idx> | c <scope idx> | L <voff> | S <line> <hex path> | U <voff> | B <section> |
   N <section> | D <scope idx> | u <unit idx> | T <type idx> | P <procedure idx> | M <udt idx> |
   G <section> <symbol idx> | O <voff> | V <scope idx> */
#include "../dbg/rdi.h"
#include "../dbg/load.h"
#include <stdbool.h>

/* a type by meaning: built in: its name and size; made from another: kind, flags, count, size,
   the other type, then a function's parameters; with a name: kind, name, size, what it stands
   on; a bit field: first bit, count, size, the type. A type refers only to types before it. */
static void type(const Rdi *r, uint32_t idx)
{
  const RdiType *t = rdi_at(r, RDI_TYPE_NODES, RdiType, idx);
  RdiSpan name = rdi_str(r, t->a);
  if (idx == 0 || t->kind == 0) printf("none");   /* RAD writes a type of kind 0 for "no type" */
  else if (t->kind < 0x1000) printf("%.*s:%u", (int)name.n, (const char *)name.p, t->byte_size);
  else if (t->kind < 0x2000 || t->kind == 0xf000) {
    if (t->kind == 0xf000) printf("bits %u+%u:%u of ", t->b, t->c, t->byte_size);
    else printf("%x.%x[%u]:%u ", t->kind, t->flags, t->b, t->byte_size);
    if (t->a < idx) type(r, t->a); else printf("?");
    RdiSpan params = rdi_run(r, RDI_INDEX_RUNS, t->c, t->kind == 0x1005 || t->kind == 0x1006 ? t->b : 0);
    for (uint64_t i = 0; i < params.n; i += 1) {
      printf(i ? ", " : " (");
      if (((const uint32_t *)params.p)[i] < idx) type(r, ((const uint32_t *)params.p)[i]); else printf("?");
    }
    if (params.n) printf(")");
  } else {
    printf("%x %.*s:%u", t->kind, (int)name.n, (const char *)name.p, t->byte_size);
    if (t->b != 0 && t->b < idx) { printf(" on "); type(r, t->b); }
  }
}

/* a variable or constant by meaning: name, whether other files see it, the kind of its place and the offset (a constant's value), its type */
static void variable(const Rdi *r, int section, uint64_t idx)
{
  const RdiSymbol *y = rdi_at(r, section, RdiSymbol, idx);
  RdiSpan name = rdi_str(r, y->name);
  uint64_t off = y->location & 0x00ffffffffffffff;
  fwrite(name.p, 1, name.n, stdout);
  printf("\t%u %u %llx\t", y->container_flags >> 7, (unsigned)(y->location >> 56), (unsigned long long)(y->location >> 56 == 8 ? *rdi_at(r, RDI_LOCATION_CONSTANTS, uint64_t, off / 8) : off));
  type(r, y->type);
}

/* where a local is: none; `3 reg off` at register + offset, `4` at the address stored there, `5 reg` in the register; `1` then the
   bytecode's operations as op:operand; a set as [first-end place] for each case */
static void place(const Rdi *r, uint64_t l, bool in_set)
{
  static const uint8_t operand[64] = {[4] = 1, [5] = 4, [11] = 1, [12] = 2, [13] = 4, [21] = 2, [42] = 1};   /* bytes after each operation RAD writes for a place */
  unsigned kind = (unsigned)(l >> 56);
  if (kind == 9 && !in_set) {
    for (uint64_t i = 0; i < (l >> 32 & 0xffffff); i += 1) {
      const RdiLocationCase *c = rdi_at(r, RDI_LOCATION_SETS, RdiLocationCase, (l & 0xffffffff) + i);
      printf("[%llx-%llx ", (unsigned long long)c->first, (unsigned long long)c->opl);
      place(r, c->location, true);
      printf("]");
    }
  } else if (kind == 1) {
    printf("1");
    for (uint64_t at = l & 0x00ffffffffffffff, steps = 0; steps < 16; steps += 1) {
      RdiSpan op = rdi_run(r, RDI_LOCATION_BYTECODE, at, 1);
      if (op.n == 0 || *(const uint8_t *)op.p == 0) break;
      RdiSpan p = rdi_run(r, RDI_LOCATION_BYTECODE, at + 1, operand[*(const uint8_t *)op.p & 63]);
      printf(" %u:", *(const uint8_t *)op.p);
      for (uint64_t i = p.n; i > 0; i -= 1) printf("%02x", ((const uint8_t *)p.p)[i - 1]);
      at += 1 + p.n;
    }
  } else if (kind == 3 || kind == 4) printf("%u %u %d", kind, (unsigned)(l >> 48 & 0xff), (int16_t)(l >> 32 & 0xffff));
  else if (kind == 5) printf("5 %u", (unsigned)(l >> 48 & 0xff));
  else printf(kind ? "?%u" : "none", kind);
}

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
    } else if (op == 'T') type(&r, (uint32_t)a);
    else if (op == 'P') {     /* a procedure by meaning: its name, then its type */
      const RdiSymbol *y = rdi_at(&r, RDI_PROCEDURES, RdiSymbol, a);
      s = rdi_str(&r, y->name);
      fwrite(s.p, 1, s.n, stdout);
      putchar('\t');
      type(&r, y->type);
    } else if (op == 'M') {   /* a record of members by meaning: its type and whether that type names it, its flags, then each member */
      const RdiUdt *u = rdi_at(&r, RDI_UDTS, RdiUdt, a);
      type(&r, u->self_type);
      printf("\t%d %u\t", rdi_at(&r, RDI_TYPE_NODES, RdiType, u->self_type)->c == a, u->flags);
      for (uint32_t i = u->member_first; i - u->member_first < u->member_count; i += 1) {
        const RdiMember *m = rdi_at(&r, RDI_MEMBERS, RdiMember, i);
        const RdiEnumMember *e = rdi_at(&r, RDI_ENUM_MEMBERS, RdiEnumMember, i);
        s = rdi_str(&r, u->flags & 1 ? e->name : m->name);
        fwrite(s.p, 1, s.n, stdout);
        if (u->flags & 1) printf("=%llu; ", (unsigned long long)e->val);
        else { printf(" %x+%u ", m->kind, m->off); type(&r, m->type); printf("; "); }
      }
    } else if (op == 'V') {   /* a scope's locals by meaning: name, whether a parameter, type, place */
      const RdiScope *c = rdi_at(&r, RDI_SCOPES, RdiScope, a);
      for (uint32_t i = c->local_first; i - c->local_first < c->local_count; i += 1) {
        const RdiSymbol *y = rdi_at(&r, RDI_LOCALS, RdiSymbol, i);
        s = rdi_str(&r, y->name);
        fwrite(s.p, 1, s.n, stdout);
        printf(" %u %u.%u ", y->flags, y->container_flags, y->container == a);
        type(&r, y->type);
        printf(" @ ");
        place(&r, y->location, false);
        printf("; ");
      }
    } else if (op == 'G' && scanf("%llu", &b) == 1) variable(&r, (int)a < RDI_SECTIONS ? (int)a : 0, b);
    else if (op == 'O') variable(&r, RDI_GLOBALS, rdi_vmap(&r, RDI_GLOBAL_VMAP, a));   /* the global variable at an offset */
    else if (op == 'p') {   /* a procedure: root scope, then its name */
      const RdiSymbol *y = rdi_at(&r, RDI_PROCEDURES, RdiSymbol, a);
      s = rdi_str(&r, y->name);
      printf("%u ", y->root_scope);
      fwrite(s.p, 1, s.n, stdout);
    } else if (op == 'c') {   /* a scope: procedure, child, sibling, then its offset ranges */
      const RdiScope *c = rdi_at(&r, RDI_SCOPES, RdiScope, a);
      printf("%u %u %u", c->proc, c->first_child, c->next_sibling);
      s = rdi_run(&r, RDI_SCOPE_VOFFS, c->voff_first, c->voff_opl - c->voff_first);
      for (uint64_t i = 0; i < s.n; i += 1) printf(" %llu", (unsigned long long)((const uint64_t *)s.p)[i]);
    } else if (op == 'L') {   /* the line at an offset, by meaning: the file's path, then the line */
      RdiLine l = rdi_line_from_voff(&r, a);
      uint64_t len = rdi_path(&r, rdi_at(&r, RDI_SOURCE_FILES, RdiSourceFile, l.file)->path_node, hex, sizeof hex);
      printf("%.*s %u", (int)len, hex, l.line);
    } else if (op == 'S' && scanf("%65535s", hex) == 1) {   /* S <line> <hex normal path>: offsets of exactly that line */
      uint64_t len = 0;
      for (; hex[1 + 2 * len]; len += 1) { unsigned c; sscanf(hex + 1 + 2 * len, "%2x", &c); hex[len] = (char)c; }
      s = rdi_matches(&r, RDI_NAMES_SOURCE_PATHS, hex, len);
      uint32_t file = s.n ? *(const uint32_t *)s.p : 0;
      const RdiSourceLineMap *m = rdi_at(&r, RDI_SOURCE_LINE_MAPS, RdiSourceLineMap, rdi_at(&r, RDI_SOURCE_FILES, RdiSourceFile, file)->line_map);
      RdiSpan nums = rdi_run(&r, RDI_SOURCE_LINE_NUMS, m->nums_base, m->line_count);
      bool has = false;
      for (uint64_t i = 0; i < nums.n; i += 1) has |= ((const uint32_t *)nums.p)[i] == a;
      s = has ? rdi_voffs_from_line(&r, file, (uint32_t)a) : (RdiSpan){0, 0};
      for (uint64_t i = 0; i < s.n; i += 1) printf(" %llu", (unsigned long long)((const uint64_t *)s.p)[i]);
    } else if (op == 'U') {   /* the name of the unit at an offset */
      s = rdi_str(&r, rdi_at(&r, RDI_UNITS, RdiUnit, rdi_vmap(&r, RDI_UNIT_VMAP, a))->name);
      fwrite(s.p, 1, s.n, stdout);
    } else if (op == 'B') {   /* a binary section */
      const RdiSection *b = rdi_at(&r, RDI_BINARY_SECTIONS, RdiSection, a);
      s = rdi_str(&r, b->name);
      printf("%.*s %u %llx %llx %llx %llx", (int)s.n, (const char *)s.p, b->flags, (unsigned long long)b->voff, (unsigned long long)b->voff_opl, (unsigned long long)b->foff, (unsigned long long)b->foff_opl);
    } else if (op == 'N') printf("%llu", (unsigned long long)r.s[a < RDI_SECTIONS ? a : 0].n);
    else if (op == 'D') {     /* a scope by meaning: parent, inline site, child, sibling, ranges; then its procedure's flags, name, link name; then the inlined function's name; then its lines */
      const RdiScope *c = rdi_at(&r, RDI_SCOPES, RdiScope, a);
      const RdiSymbol *y = rdi_at(&r, RDI_PROCEDURES, RdiSymbol, c->proc);
      printf("%u %u %u %u %u", c->parent, c->inline_site, c->first_child, c->next_sibling, c->proc);
      s = rdi_run(&r, RDI_SCOPE_VOFFS, c->voff_first, c->voff_opl - c->voff_first);
      for (uint64_t i = 0; i < s.n; i += 1) printf(" %llx", (unsigned long long)((const uint64_t *)s.p)[i]);
      printf("\t%u %u %d\t", y->flags, y->container_flags, rdi_at(&r, RDI_SCOPES, RdiScope, y->root_scope) == c);
      s = rdi_str(&r, y->name);
      fwrite(s.p, 1, s.n, stdout);
      putchar('\t');
      s = rdi_str(&r, y->link_name);
      fwrite(s.p, 1, s.n, stdout);
      putchar('\t');
      const RdiInlineSite *site = rdi_at(&r, RDI_INLINE_SITES, RdiInlineSite, c->inline_site);
      s = rdi_str(&r, site->name);
      fwrite(s.p, 1, s.n, stdout);
      putchar('\t');
      const RdiLineTable *t = rdi_at(&r, RDI_LINE_TABLES, RdiLineTable, site->line_table);
      s = rdi_run(&r, RDI_LINE_VOFFS, t->voffs_base, t->lines_count);
      for (uint64_t i = 0; i < s.n; i += 1) {   /* what its own line table answers at each offset the table names */
        uint64_t voff = ((const uint64_t *)s.p)[i];
        if (i + 1 < s.n && ((const uint64_t *)s.p)[i + 1] == voff) continue;
        RdiLine l = rdi_line_in(&r, site->line_table, voff);
        uint64_t len = rdi_path(&r, rdi_at(&r, RDI_SOURCE_FILES, RdiSourceFile, l.file)->path_node, hex, sizeof hex);
        printf("%llx %.*s %u;", (unsigned long long)voff, (int)len, hex, l.line);
      }
    } else if (op == 'u') {   /* a unit: its procedures, then its name */
      const RdiUnit *u = rdi_at(&r, RDI_UNITS, RdiUnit, a);
      s = rdi_str(&r, u->name);
      printf("%u %u ", u->procs_first, u->procs_count);
      fwrite(s.p, 1, s.n, stdout);
    } else if (op == 't') { s = rdi_str(&r, (uint32_t)a); fwrite(s.p, 1, s.n, stdout); }
    putchar('\n');
  }
  free(buf);
  return 0;
}
