/* A scope covers nothing when it has no address or no bytes. The vmap builder against its definition on random ranges, nested or not: at every offset the
   answer is the covering range that sorts last, 0 where none covers, and keys strictly ascend. */
#define rdi_from_pdb rdi_from_pdb_under_test   /* conv.c is compiled into this file to reach vmap_build */
#include "../dbg/conv.c"
#include <stdio.h>

int main(void)
{
  setvbuf(stdout, 0, _IONBF, 0);   /* a failure returns without freeing, and the leak report must not swallow its message */
  Out o = {0};
  pair(&o.s[RDI_SCOPE_VOFFS], 0, 16), pair(&o.s[RDI_SCOPE_VOFFS], 16, 16), pair(&o.s[RDI_SCOPE_VOFFS], 16, 32), pair(&o.s[RDI_SCOPE_VOFFS], 40, 48);
  scope(&o, 1, 0, 0, 0);
  const RdiScope *made = (const RdiScope *)o.s[RDI_SCOPES].p;
  const uint64_t *kept = (const uint64_t *)o.s[RDI_SCOPE_VOFFS].p;
  if (o.scopes.n != 64 || o.s[RDI_SCOPE_VOFFS].n != 32 || made->voff_first != 0 || made->voff_opl != 4 || kept[0] != 16 || kept[1] != 32 || kept[2] != 40 || kept[3] != 48) { printf("scope: pairs without address or bytes are not dropped\n"); return 1; }
  /* What an inlined call covers, from annotations written by hand (each line: the annotations, the base, the pairs):
     which lengths move the offset, where a piece starts, a piece that continues the one before, the end closing
     what is left, 2-byte and 4-byte operands, and the malformed ones, which add nothing. */
  static const struct { uint8_t n, a[12]; uint64_t base; uint8_t pairs; uint64_t want[6]; } sites[] = {
    {3, {11, 0x24, 0}, 0x1000, 1, {0x1004, 0x1004}},                           /* offset +4 and a line, then the end: an empty piece */
    {6, {3, 8, 4, 16, 3, 0}, 0x1000, 1, {0x1008, 0x1018}},                     /* +8, length 16; an offset at its end adds nothing */
    {8, {3, 8, 4, 4, 3, 4, 4, 2}, 0x1000, 2, {0x1008, 0x100c, 0x1010, 0x1012}},/* after a length the next offset counts from the end of the piece */
    {6, {12, 4, 8, 12, 2, 6}, 0x1000, 2, {0x1008, 0x100c, 0x100e, 0x1010}},    /* after a length with an offset, from the start of the piece (RAD: from its end) */
    {7, {11, 0x24, 11, 0x23, 12, 5, 2}, 0x1000, 1, {0x1004, 0x100e}},          /* a piece starts at the first offset since the last piece */
    {7, {3, 8, 4, 16, 12, 4, 0, 0}, 0x1000, 1, {0x1008, 0x101c}},              /* a second piece that starts where the first ended is one piece */
    {7, {3, 8, 4, 16, 12, 4, 8}, 0x1000, 2, {0x1008, 0x1018, 0x1020, 0x1024}}, /* a gap of 8 makes two */
    {5, {3, 0x81, 0x00, 4, 2}, 0x1000, 1, {0x1100, 0x1102}},                   /* a 2-byte operand: offset 0x100 */
    {8, {3, 0xc0, 0x01, 0x00, 0x00, 4, 2, 0}, 0x1000, 1, {0x11000, 0x11002}},  /* a 4-byte operand: offset 0x10000 */
    {4, {3, 8, 3, 8}, 0x1000, 1, {0x1008, 0x1010}},                            /* offsets with no length: up to the last of them, at the end of the record */
    {5, {3, 8, 3, 8, 0}, 0x1000, 1, {0x1008, 0x1010}},                         /* or at the end annotation */
    {6, {3, 8, 5, 7, 3, 4}, 0x1000, 1, {0x1008, 0x100c}},                      /* a new file moves nothing */
    {2, {3, 0x81}, 0x1000, 0, {0}},                                            /* an operand cut short */
    {4, {3, 8, 4, 0xe0}, 0x1000, 0, {0}},                                      /* a first byte no operand starts with */
    {6, {3, 8, 4, 16, 7, 1}, 0x1000, 0, {0}},                                  /* an annotation not decoded: the earlier piece goes too */
    {3, {2, 0, 0}, 0x1000, 0, {0}},                                            /* code moved out of the procedure */
    {5, {1, 8, 4, 4, 0}, 0x1000, 0, {0}},                                      /* an offset that is not a step */
    {10, {3, 0xdf, 0xff, 0xff, 0xff, 4, 0xdf, 0xff, 0xff, 0xff}, 0x1000, 1, {0x1000 + 0x1fffffffull, 0x1000 + 0x3ffffffeull}},   /* large, and still inside 32 bits */
  };
  for (unsigned i = 0; i < sizeof sites / sizeof *sites; i += 1) {
    Buf out = {0};
    pair(&out, 1, 0x1008);                                                     /* what was there stays, though a piece may start at its end */
    uint8_t *a = memcpy(malloc(sites[i].n), sites[i].a, sites[i].n);           /* exactly as long, so that reading past it is seen */
    Buf lines = {0};
    inline_pieces(&out, &lines, (PdbSpan){a, sites[i].n}, sites[i].base, 0, 0);
    const uint64_t *v = (const uint64_t *)out.p;
    if (out.n != 16 + 16 * (uint64_t)sites[i].pairs || memcmp(v + 2, sites[i].want, out.n - 16) != 0 || v[0] != 1 || v[1] != 0x1008) { printf("inline site %u: wrong pieces\n", i); return 1; }
    if (sites[i].pairs == 0 && lines.n != 0) { printf("inline site %u: lines from malformed annotations\n", i); return 1; }
    free(a), free(out.p), free(lines.p);
  }
  /* Its lines, from file 8 line 10: two lines down; an offset with one more line; another file; an offset; one line up;
     an offset with a length. A record per offset with the file and line as they then are, and one for the end. */
  static const uint8_t noted[] = {6, 4, 11, 0x24, 5, 12, 3, 4, 6, 3, 12, 2, 6};
  static const uint64_t want[] = {0x1004, 1ull << 63, 8, 13, 0x1008, 1ull << 63 | 32, 12, 13, 0x100e, 1ull << 63 | 64, 12, 12, 0x1010, 96, 0, 0};
  Buf out = {0}, lines = {0};
  inline_pieces(&out, &lines, (PdbSpan){noted, sizeof noted}, 0x1000, 8, 10);
  if (lines.n != sizeof want || memcmp(lines.p, want, sizeof want) != 0) { printf("inline site: wrong lines\n"); return 1; }
  free(out.p), free(lines.p);
  free(o.s[RDI_SCOPES].p), free(o.s[RDI_SCOPE_VOFFS].p), free(o.scopes.p);
  /* One type record at a time, each in a block of exactly its length: the shortest record of each kind that makes a type, then one
     a byte shorter, which makes none. Ids far past the last record are no type. The last two rows are argument lists read by the
     procedure after them: one that claims three ids and has one, one too short to hold its count. */
  static const struct { uint16_t kind; uint8_t n, d[20]; uint16_t want; uint8_t list; } records[] = {
    {LF_MODIFIER, 6, {0x74, 0, 0, 0, 1, 0}, 0x1000, 0}, {LF_MODIFIER, 5, {0x74, 0, 0, 0, 1}, 0, 0},
    {LF_POINTER, 8, {0xff, 0xff, 0xff, 0x7f, 0x0c, 0, 1, 0}, 0x1001, 0}, {LF_POINTER, 7, {0x74, 0, 0, 0, 0x0c, 0, 1}, 0, 0},
    {LF_PROCEDURE, 12, {0x74, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff, 0xff, 0x7f}, 0x1005, 0}, {LF_PROCEDURE, 11, {0x74}, 0, 0},
    {LF_MFUNCTION, 20, {0x74, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff, 0xff, 0x7f, 0, 0, 0, 0, 0xff, 0xff, 0xff, 0x7f}, 0x1006, 0}, {LF_MFUNCTION, 19, {0x74}, 0, 0},
    {LF_BITFIELD, 6, {0x74, 0, 0, 0, 3, 1}, 0xf000, 0}, {LF_BITFIELD, 5, {0x74, 0, 0, 0, 3}, 0, 0},
    {LF_ARRAY, 8, {0x74, 0, 0, 0, 0x23, 0, 0, 0}, 0x1004, 0}, {LF_ARRAY, 7, {0x74}, 0, 0},
    {LF_STRUCTURE, 16, {0}, 0x2000, 0}, {LF_STRUCTURE, 15, {0}, 0, 0}, {LF_CLASS, 16, {0}, 0x2001, 0}, {LF_CLASS, 15, {0}, 0, 0},
    {LF_UNION, 8, {0}, 0x2002, 0}, {LF_UNION, 7, {0}, 0, 0}, {LF_ENUM, 12, {0}, 0x2003, 0}, {LF_ENUM, 11, {0}, 0, 0},
    {LF_ARGLIST, 8, {3, 0, 0, 0, 0x74, 0, 0, 0}, 0x1005, 1}, {LF_ARGLIST, 3, {3, 0, 0}, 0x1005, 1}};
  for (unsigned i = 0; i < sizeof records / sizeof *records; i += 1) {
    static const uint8_t reader[] = {14, 0, 0x08, 0x10, 0x74, 0, 0, 0, 0, 0, 0, 0, 0x00, 0x10, 0, 0};   /* a procedure whose argument list is record 0x1000 */
    uint64_t n = 4 + (uint64_t)records[i].n, all = n + (records[i].list ? sizeof reader : 0);
    uint8_t *a = malloc(all);
    a[0] = (uint8_t)(n - 2), a[1] = 0, a[2] = (uint8_t)records[i].kind, a[3] = (uint8_t)(records[i].kind >> 8);
    memcpy(a + 4, records[i].d, records[i].n);
    if (records[i].list) memcpy(a + n, reader, sizeof reader);
    Out t = {.pointer_size = 8, .leaves = {a, (uint32_t)all}};
    add(&t.s[RDI_TYPE_NODES], sizeof(RdiType)), add(&t.s[RDI_INDEX_RUNS], 4);   /* each table's first record stands for none */
    add(&t.leaf_at, 4);
    if (records[i].list) *(uint32_t *)add(&t.leaf_at, 4) = (uint32_t)n;
    add(&t.made, 4 * (FIRST_RECORD + 2));
    for (uint32_t id = FIRST_RECORD; id < FIRST_RECORD + 2; id += 1) record_type(&t, id);
    uint32_t made = ((uint32_t *)t.made.p)[FIRST_RECORD + records[i].list];
    const RdiType *y = (const RdiType *)t.s[RDI_TYPE_NODES].p + made;
    if ((made != 0) != (records[i].want != 0) || y->kind != records[i].want) { printf("type record %u: made a type of kind %x\n", i, y->kind); return 1; }
    if (records[i].list && (y->b != (records[i].n == 8) || (y->b && ((uint32_t *)t.s[RDI_INDEX_RUNS].p)[y->c] == 0))) { printf("type record %u: wrong parameters\n", i); return 1; }
    if (records[i].kind == LF_POINTER && y->a != 0) { printf("type record %u: a type for an id past the last\n", i); return 1; }
    Buf *used[] = {&t.s[RDI_TYPE_NODES], &t.s[RDI_INDEX_RUNS], &t.leaf_at, &t.made, &t.strings.keys, &t.strings.at, &t.strings.slots};
    for (unsigned k = 0; k < sizeof used / sizeof *used; k += 1) free(used[k]->p);
    free(a);
  }
  /* Field lists, each in a block of exactly its length: record 0x1000 is `before` (a method list, or the list that continues),
     0x1001 the field list, 0x1002 the struct or enum that has it. Wanted: how many members, and the last one's kind and offset
     (for an enum, its value). `list` says which of the two lists the struct names. M is a member `a` at offset 8, 12 bytes; E an enumerator `a` with the value -1, 8 bytes. */
#define M 0x0d, 0x15, 3, 0, 0x74, 0, 0, 0, 8, 0, 'a', 0
#define E 0x02, 0x15, 3, 0, 0x00, 0x80, 0xff, 0
  static const struct { uint8_t values, list, before_kind, before_n, before[24], n, d[40], count; uint16_t kind; uint64_t off; } lists[] = {
    {0, 1, 3, 0, {0}, 12, {M}, 1, 1, 8}, {0, 1, 3, 0, {0}, 11, {M}, 1, 1, 8}, {0, 1, 3, 0, {0}, 7, {M}, 0, 0, 0},
    {0, 1, 3, 0, {0}, 22, {M, 0x00, 0x14, 3, 0, 0x74, 0, 0, 0, 4, 0}, 2, 0x201, 4},                                   /* a base */
    {0, 1, 3, 0, {0}, 28, {0x01, 0x14, 3, 0, 0x74, 0, 0, 0, 0x74, 0, 0, 0, 8, 0, 1, 0, M}, 2, 1, 8},                  /* a virtual base and its two numbers */
    {0, 1, 3, 0, {0}, 28, {0x02, 0x14, 3, 0, 0x74, 0, 0, 0, 0x74, 0, 0, 0, 8, 0, 1, 0, M}, 2, 1, 8},
    {0, 1, 3, 0, {0}, 16, {0x02, 0x14, 3, 0, 0x74, 0, 0, 0, 0x74, 0, 0, 0, 8, 0, 1, 0}, 1, 0x202, 0},
    {0, 1, 3, 0, {0}, 10, {0x0e, 0x15, 3, 0, 0x74, 0, 0, 0, 'a', 0}, 1, 2, 0}, {0, 1, 3, 0, {0}, 10, {0x10, 0x15, 0, 0, 0x74, 0, 0, 0, 'a', 0}, 1, 0x300, 0},
    {0, 1, 3, 0, {0}, 10, {0x12, 0x15, 3, 0, 0x74, 0, 0, 0, 'a', 0}, 1, 0x300, 0},
    {0, 1, 3, 0, {0}, 20, {0x09, 0x14, 0, 0, 0x74, 0, 0, 0, M}, 1, 1, 8},                                             /* a table of virtual methods is no member */
    {0, 1, 3, 0, {0}, 22, {0x11, 0x15, 0x0b, 0, 0x74, 0, 0, 0, 'a', 0, 0xf2, 0xf1, 0x11, 0x15, 3, 0, 0x74, 0, 0, 0, 'a', 0}, 2, 0x100, 0},   /* a static method, a plain one */
    {0, 1, 3, 0, {0}, 28, {0x11, 0x15, 0x13, 0, 0x74, 0, 0, 0, 9, 9, 9, 9, 'a', 0, 0xf2, 0xf1, M}, 2, 1, 8},          /* a new virtual method has 4 more bytes */
    {0, 1, 3, 0, {0}, 28, {0x11, 0x15, 0x1b, 0, 0x74, 0, 0, 0, 9, 9, 9, 9, 'a', 0, 0xf2, 0xf1, M}, 2, 1, 8},
    {0, 1, 3, 0, {0}, 22, {0x11, 0x15, 0x07, 0, 0x74, 0, 0, 0, 'a', 0, 0xf2, 0xf1, 0x11, 0x15, 0x17, 0, 0x74, 0, 0, 0, 'a', 0}, 2, 0x102, 0},
    {0, 1, 6, 20, {0x13, 0, 0, 0, 0x74, 0, 0, 0, 9, 9, 9, 9, 0x0b, 0, 0, 0, 0x74, 0, 0, 0}, 10, {0x0f, 0x15, 2, 0, 0x00, 0x10, 0, 0, 'a', 0}, 2, 0x101, 0},   /* two methods of one name */
    {0, 1, 6, 19, {0x13, 0, 0, 0, 0x74, 0, 0, 0, 9, 9, 9, 9, 0x0b, 0, 0, 0, 0x74, 0, 0}, 10, {0x0f, 0x15, 2, 0, 0x00, 0x10, 0, 0, 'a', 0}, 1, 0x102, 0},
    {0, 1, 3, 8, {0x0b, 0, 0, 0, 0x74, 0, 0, 0}, 10, {0x0f, 0x15, 2, 0, 0x00, 0x10, 0, 0, 'a', 0}, 0, 0, 0},          /* a method list that is none */
    {0, 1, 6, 8, {0x13, 0, 0, 0, 0x74, 0, 0, 0}, 10, {0x0f, 0x15, 2, 0, 0x00, 0x10, 0, 0, 'a', 0}, 1, 0x102, 0},      /* one that ends before its last 4 bytes */
    {0, 1, 6, 12, {M}, 20, {M, 0x04, 0x14, 0, 0, 0x00, 0x10, 0, 0}, 1, 1, 8},                                         /* continues in what is no field list */
    {0, 0, 3, 20, {M, 0x04, 0x14, 0, 0, 0x01, 0x10, 0, 0}, 12, {M}, 1, 1, 8},                                         /* continues in a later list */
    {0, 1, 3, 0, {0}, 22, {0x11, 0x15, 0x0f, 0, 0x74, 0, 0, 0, 'a', 0, 0xf2, 0xf1, 0x11, 0x15, 0x0b, 0, 0x74, 0, 0, 0, 'a', 0}, 2, 0x101, 0},   /* a friend */
    {0, 1, 3, 0, {0}, 22, {0x11, 0x15, 0x0b, 0, 0x74, 0, 0, 0, 'a', 0, 0xf2, 0xf1, 0x11, 0x15, 0x0f, 0, 0x74, 0, 0, 0, 'a', 0}, 2, 0x100, 0},
    {0, 1, 3, 0, {0}, 22, {0x11, 0x15, 0x1f, 0, 0x74, 0, 0, 0, 'a', 0, 0xf2, 0xf1, 0x11, 0x15, 0x0b, 0, 0x74, 0, 0, 0, 'a', 0}, 2, 0x101, 0},   /* the method kind no compiler writes */
    {0, 1, 3, 0, {0}, 22, {0x11, 0x15, 0x0b, 0, 0x74, 0, 0, 0, 'a', 0, 0xf2, 0xf1, 0x11, 0x15, 0x1f, 0, 0x74, 0, 0, 0, 'a', 0}, 2, 0x100, 0},
    {0, 1, 3, 12, {M}, 20, {M, 0x04, 0x14, 0, 0, 0x00, 0x10, 0, 0}, 2, 1, 8},                                         /* continues in the list before it */
    {0, 1, 3, 12, {M}, 32, {M, 0x04, 0x14, 0, 0, 0x00, 0x10, 0, 0, M}, 2, 1, 8},                                      /* and nothing after that is read */
    {0, 1, 3, 12, {M}, 20, {M, 0x04, 0x14, 0, 0, 0x01, 0x10, 0, 0}, 1, 1, 8}, {0, 1, 3, 12, {M}, 20, {M, 0x04, 0x14, 0, 0, 0x02, 0x10, 0, 0}, 1, 1, 8},   /* in itself, in a later record */
    {0, 1, 3, 0, {0}, 32, {M, 0xff, 0x15, 0, 0, 0, 0, 0, 0, M}, 1, 1, 8},                                             /* a kind not known ends the list */
    {0, 1, 3, 0, {0}, 20, {M, E}, 1, 1, 8}, {1, 1, 3, 0, {0}, 20, {E, M}, 1, 0, UINT64_MAX}, {1, 1, 3, 0, {0}, 16, {E, E}, 2, 0, UINT64_MAX},
    {1, 1, 3, 8, {E}, 16, {E, 0x04, 0x14, 0, 0, 0x00, 0x10, 0, 0}, 2, 0, UINT64_MAX}};
  for (unsigned i = 0; i < sizeof lists / sizeof *lists; i += 1) {
    static const uint8_t record[2][20] = {{18, 0, 0x05, 0x15, 0, 0, 0, 0, 0x01, 0x10}, {14, 0, 0x07, 0x15, 0, 0, 0, 0, 0x74, 0, 0, 0, 0x01, 0x10}};
    uint32_t first = 4 + (uint32_t)lists[i].before_n, second = first + 4 + lists[i].n, all = second + record[lists[i].values][0] + 2;
    uint8_t *a = malloc(all);
    a[0] = (uint8_t)(first - 2), a[1] = 0, a[2] = lists[i].before_kind, a[3] = 0x12;
    memcpy(a + 4, lists[i].before, lists[i].before_n);
    a[first] = (uint8_t)(lists[i].n + 2), a[first + 1] = 0, a[first + 2] = 0x03, a[first + 3] = 0x12;
    memcpy(a + first + 4, lists[i].d, lists[i].n);
    memcpy(a + second, record[lists[i].values], all - second);
    a[second + (lists[i].values ? 12 : 8)] = lists[i].list;
    Out t = {.pointer_size = 8, .leaves = {a, all}};
    add(&t.s[RDI_TYPE_NODES], sizeof(RdiType)), add(&t.s[RDI_UDTS], sizeof(RdiUdt)), add(&t.s[RDI_MEMBERS], sizeof(RdiMember)), add(&t.s[RDI_ENUM_MEMBERS], sizeof(RdiEnumMember));
    *(uint32_t *)add(&t.leaf_at, 4) = 0, *(uint32_t *)add(&t.leaf_at, 4) = first, *(uint32_t *)add(&t.leaf_at, 4) = second;
    add(&t.made, 4 * (FIRST_RECORD + 3));
    for (uint32_t id = FIRST_RECORD; id < FIRST_RECORD + 3; id += 1) record_type(&t, id);
    for (uint32_t id = FIRST_RECORD; id < FIRST_RECORD + 3; id += 1) members(&t, id);
    const RdiUdt *u = (const RdiUdt *)t.s[RDI_UDTS].p + 1;
    const RdiMember *m = (const RdiMember *)t.s[RDI_MEMBERS].p + u->member_first + u->member_count - 1;
    const RdiEnumMember *e = (const RdiEnumMember *)t.s[RDI_ENUM_MEMBERS].p + u->member_first + u->member_count - 1;
    bool ok = t.s[RDI_UDTS].n == 2 * sizeof *u && u->member_count == lists[i].count && u->member_first == 1 && u->flags == (lists[i].values && lists[i].count)
              && ((const RdiType *)t.s[RDI_TYPE_NODES].p)[u->self_type].c == 1 && t.s[lists[i].values ? RDI_MEMBERS : RDI_ENUM_MEMBERS].n == 16;
    if (ok && lists[i].count) ok = lists[i].values ? e->val == lists[i].off : m->kind == lists[i].kind && m->off == lists[i].off && m->type != 0;
    if (!ok) { printf("field list %u: wrong members\n", i); return 1; }
    Buf *used[] = {&t.s[RDI_TYPE_NODES], &t.s[RDI_UDTS], &t.s[RDI_MEMBERS], &t.s[RDI_ENUM_MEMBERS], &t.leaf_at, &t.made, &t.strings.keys, &t.strings.at, &t.strings.slots};
    for (unsigned k = 0; k < sizeof used / sizeof *used; k += 1) free(used[k]->p);
    free(a);
  }
  /* A number in a type record: the 2 bytes themselves below 0x8000, else 1, 2, 4 or 8 bytes that follow; 0 when cut short or of
     a kind not read. The signed kinds are widened with their sign. Each in a block of exactly its length. */
  static const struct { uint8_t n, a[10], after; uint64_t want; } numbers[] = {
    {2, {0x10, 0x00}, 2, 16}, {2, {0xff, 0x7f}, 2, 0x7fff}, {3, {0x00, 0x80, 0xfe}, 3, UINT64_MAX - 1}, {3, {0x00, 0x80, 0x7f}, 3, 0x7f}, {3, {0x00, 0x80, 0x80}, 3, UINT64_MAX - 0x7f}, {4, {0x02, 0x80, 0x34, 0x12}, 4, 0x1234},
    {6, {0x04, 0x80, 4, 3, 2, 1}, 6, 0x01020304}, {4, {0x01, 0x80, 0x34, 0x12}, 4, 0x1234}, {6, {0x03, 0x80, 4, 3, 2, 1}, 6, 0x01020304},
    {4, {0x01, 0x80, 0xfe, 0xff}, 4, UINT64_MAX - 1}, {4, {0x02, 0x80, 0xfe, 0xff}, 4, 0xfffe}, {6, {0x03, 0x80, 0xfe, 0xff, 0xff, 0xff}, 6, UINT64_MAX - 1},
    {6, {0x04, 0x80, 0xfe, 0xff, 0xff, 0xff}, 6, 0xfffffffe}, {10, {0x09, 0x80, 0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}, 10, UINT64_MAX - 1},
    {10, {0x09, 0x80, 8, 7, 6, 5, 4, 3, 2, 1}, 10, 0x0102030405060708}, {10, {0x0a, 0x80, 8, 7, 6, 5, 4, 3, 2, 1}, 10, 0x0102030405060708},
    {5, {0x04, 0x80, 4, 3, 2}, 2, 0}, {4, {0x05, 0x80, 1, 2}, 2, 0}, {4, {0x0b, 0x80, 1, 2}, 2, 0}, {1, {0x10}, 0, 0}, {0, {0}, 0, 0}};
  for (unsigned i = 0; i < sizeof numbers / sizeof *numbers; i += 1) {
    uint8_t *a = memcpy(malloc(numbers[i].n + 1), numbers[i].a, numbers[i].n) ;
    uint32_t at = 0;
    uint64_t got = numeric((PdbSpan){a, numbers[i].n}, &at);
    free(a);
    if (got != numbers[i].want || at != numbers[i].after) { printf("number %u: wrong value or length\n", i); return 1; }
  }
  uint32_t seed = 1;
  for (int round = 0; round < 200000; round += 1) {
    Buf ranges = {0}, out = {0};
    uint64_t r[12][4];
    int n = 0;
    for (int count = (seed = seed * 1103515245 + 12345) >> 16 & 7; n < count; n += 1) {
      uint64_t first = (seed = seed * 1103515245 + 12345) >> 16 & 31, size = 1 + ((seed = seed * 1103515245 + 12345) >> 16 & 15);
      r[n][0] = first, r[n][1] = ~(first + size), r[n][2] = (uint64_t)n + 1, r[n][3] = 0;
      rec(&ranges, r[n][0], r[n][1], r[n][2], 0);
    }
    vmap_build(&out, &ranges);
    const RdiVMap *v = (const RdiVMap *)out.p;
    uint64_t entries = out.n / sizeof *v;
    for (uint64_t i = 1; i < entries; i += 1)
      if (v[i - 1].voff >= v[i].voff) { printf("round %d: keys do not ascend\n", round); return 1; }
    for (uint64_t voff = 0; voff < 50; voff += 1) {
      uint64_t want = 0, got = 0;
      const uint64_t *best = 0;
      for (int i = 0; i < n; i += 1)
        if (r[i][0] <= voff && voff < ~r[i][1] && (!best || rec_cmp(r[i], best) > 0)) best = r[i], want = r[i][2];
      for (uint64_t i = 0; i + 1 < entries; i += 1) if (v[i].voff <= voff) got = v[i].idx;   /* the last entry only ends the map */
      if (entries && voff >= v[entries - 1].voff) got = 0;
      if (got != want) { printf("round %d offset %llu: want %llu, got %llu\n", round, (unsigned long long)voff, (unsigned long long)want, (unsigned long long)got); return 1; }
    }
    free(ranges.p);
    free(out.p);
  }
  printf("vmap: 200000 random range sets agree with the definition\n");
  return 0;
}
