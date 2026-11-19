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
    /* a list cut inside its only field, so that the next field would start past its end, where the next record happens to read as a nested type */
    {0, 0, 3, 9, {0x0d, 0x15, 3, 0, 0x74, 0, 0, 0, 8}, 9, {0x15, 0, 0, 0x74, 0, 0, 0, 'a', 0}, 1, 1, 0},
    {0, 1, 3, 0, {0}, 24, {0x11, 0x15, 3, 0, 0x74, 0, 0, 0, 'a', 0, 0xf2, 0xf1, M}, 2, 1, 8},                         /* a plain method */
    {0, 1, 3, 0, {0}, 22, {M, 0x00, 0x14, 3, 0, 0x74, 0, 0, 0, 4, 0}, 2, 0x201, 4},                                   /* a base */
    {0, 1, 3, 0, {0}, 32, {0x01, 0x14, 3, 0, 0x74, 0, 0, 0, 0x74, 0, 0, 0, 0x02, 0x80, 8, 0, 1, 0, 0xf2, 0xf1, M}, 2, 1, 8},   /* a virtual base and its two numbers */
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
    {0, 1, 6, 20, {0x1b, 0, 0, 0, 0x74, 0, 0, 0, 9, 9, 9, 9, 0x0b, 0, 0, 0, 0x74, 0, 0, 0}, 10, {0x0f, 0x15, 2, 0, 0x00, 0x10, 0, 0, 'a', 0}, 2, 0x101, 0},
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
    {0, 1, 3, 0, {0}, 20, {M, E}, 1, 1, 8}, {1, 1, 3, 0, {0}, 20, {E, M}, 1, 0, UINT64_MAX}, {1, 1, 3, 0, {0}, 16, {E, E}, 2, 0, UINT64_MAX}, {1, 1, 3, 0, {0}, 12, {M}, 0, 0, 0},
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
  /* Variable, constant and typedef symbols, each in a block of exactly its length, in an image whose one section starts at 0x1000.
     Wanted: the table that gains one record (0: none), its flags, its place (a constant's value), its name, how many types there
     are afterwards (2 for `int`, 1 for the enum `of` that a row sets up as type 0x1000), and the bytes a global covers. */
#define INT 0x74, 0, 0, 0
#define AT8 8, 0, 0, 0, 1, 0   /* offset 8 in section 1 */
  static const struct { uint16_t kind; uint8_t global, n, d[16]; const char *of; int table; uint8_t flags; uint64_t place; const char *name; uint8_t nodes, covers; } symbols[] = {
    {S_GDATA32, 1, 12, {INT, AT8, 'g', 0}, 0, RDI_GLOBALS, 0x80, 0x1008, "g", 3, 4}, {S_LDATA32, 0, 12, {INT, AT8, 'g', 0}, 0, RDI_GLOBALS, 0, 0x1008, "g", 3, 4},
    {S_GDATA32, 1, 11, {INT, AT8, 'g'}, 0, RDI_GLOBALS, 0x80, 0x1008, "g", 3, 4}, {S_GDATA32, 1, 10, {INT, AT8}, 0, RDI_GLOBALS, 0x80, 0x1008, "", 3, 4},
    {S_GDATA32, 1, 9, {INT, AT8}, 0, 0, 0, 0, 0, 1, 0}, {S_GDATA32, 1, 12, {0, 0, 0, 0, AT8, 'g', 0}, 0, RDI_GLOBALS, 0x80, 0x1008, "g", 1, 0},
    {S_GDATA32, 1, 12, {INT, 8, 0, 0, 0, 2, 0, 'g', 0}, 0, RDI_GLOBALS, 0x80, 0, "g", 3, 4},     /* a section the image does not have */
    {S_GDATA32, 1, 12, {0, 0x10, 0, 0, AT8, 'g', 0}, "a::E", RDI_GLOBALS, 0x80, 0x1008, "g", 2, 4},
    {S_GTHREAD32, 1, 12, {INT, AT8, 't', 0}, 0, RDI_THREAD_VARS, 0x80, 8, "t", 3, 0}, {S_LTHREAD32, 0, 12, {INT, AT8, 't', 0}, 0, RDI_THREAD_VARS, 0, 8, "t", 3, 0},
    {S_LTHREAD32, 0, 9, {INT, AT8}, 0, 0, 0, 0, 0, 1, 0},
    {S_CONSTANT, 1, 8, {INT, 7, 0, 'c', 0}, 0, RDI_CONSTANTS, 0, 7, "c", 3, 0}, {S_CONSTANT, 0, 9, {INT, 0, 0x80, 0xff, 'c', 0}, 0, RDI_CONSTANTS, 0, UINT64_MAX, "c", 3, 0},
    {S_CONSTANT, 1, 8, {0, 0x10, 0, 0, 7, 0, 'c', 0}, "a::B<c::d>::E", RDI_CONSTANTS, 0, 7, "a::B<c::d>::c", 2, 0},
    {S_CONSTANT, 1, 8, {0, 0x10, 0, 0, 7, 0, 'c', 0}, "B<c::d>", RDI_CONSTANTS, 0, 7, "c", 2, 0}, {S_CONSTANT, 1, 8, {0, 0x10, 0, 0, 7, 0, 'c', 0}, "a::B<x::C<d>>", RDI_CONSTANTS, 0, 7, "a::c", 2, 0}, {S_CONSTANT, 1, 8, {0, 0x10, 0, 0, 7, 0, 'c', 0}, "a::<b", RDI_CONSTANTS, 0, 7, "a::c", 2, 0},
    {S_CONSTANT, 1, 6, {0, 0x10, 0, 0, 7, 0}, "a::E", RDI_CONSTANTS, 0, 7, "a::", 2, 0}, {S_CONSTANT, 1, 8, {0, 0x10, 0, 0, 7, 0, 'c', 0}, "::E", RDI_CONSTANTS, 0, 7, "::c", 2, 0}, {S_CONSTANT, 1, 6, {INT, 7, 0}, 0, 0, 0, 0, 0, 3, 0},
    {S_CONSTANT, 1, 4, {INT}, 0, 0, 0, 0, 0, 3, 0}, {S_CONSTANT, 1, 3, {INT}, 0, 0, 0, 0, 0, 1, 0},
    {S_UDT, 1, 6, {INT, 'u', 0}, 0, RDI_TYPE_NODES, 0, 0, "u", 4, 0}, {S_UDT, 0, 6, {INT, 'u', 0}, 0, 0, 0, 0, 0, 1, 0}, {S_UDT, 1, 3, {INT}, 0, 0, 0, 0, 0, 1, 0},
    {S_PUB32, 1, 12, {INT, AT8, 'p', 0}, 0, 0, 0, 0, 0, 1, 0}};
  static const uint8_t header[40] = {[12] = 0x00, 0x10};   /* an image section header: its address */
  static const int tables[] = {RDI_GLOBALS, RDI_THREAD_VARS, RDI_CONSTANTS};
  for (unsigned i = 0; i < sizeof symbols / sizeof *symbols; i += 1) {
    uint8_t *a = memcpy(malloc(symbols[i].n), symbols[i].d, symbols[i].n), *sec = memcpy(malloc(sizeof header), header, sizeof header);
    Pdb pdb = {.sec = sec, .sections = 1};
    Out t = {.pointer_size = 8};
    for (unsigned k = 0; k < 3; k += 1) add(&t.s[tables[k]], sizeof(RdiSymbol));
    add(&t.s[RDI_TYPE_NODES], sizeof(RdiType)), add(&t.s[RDI_LOCATION_CONSTANTS], 8), add(&t.made, 4 * (FIRST_RECORD + 1));
    intern(&t.strings, "", 0);
    if (symbols[i].of) ((uint32_t *)t.made.p)[FIRST_RECORD] = type_node(&t, (RdiType){.kind = 0x2003, .byte_size = 4, .a = intern(&t.strings, symbols[i].of, (uint32_t)strlen(symbols[i].of))});
    data_symbol(&t, &pdb, symbols[i].kind, (PdbSpan){a, symbols[i].n}, symbols[i].global);
    bool ok = COUNT(t.s[RDI_TYPE_NODES], RdiType) == symbols[i].nodes && t.globals.n == (symbols[i].table == RDI_GLOBALS ? 32u : 0u) && t.s[RDI_LOCATION_CONSTANTS].n == (symbols[i].table == RDI_CONSTANTS ? 16u : 8u);
    for (unsigned k = 0; k < 3; k += 1) ok = ok && COUNT(t.s[tables[k]], RdiSymbol) == 1u + (symbols[i].table == tables[k]);
    const uint32_t *starts = (const uint32_t *)t.strings.at.p;
    if (ok && symbols[i].table) {
      const RdiType *y = LAST(t.s[RDI_TYPE_NODES], RdiType);
      const RdiSymbol *v = LAST(t.s[symbols[i].table == RDI_TYPE_NODES ? RDI_GLOBALS : symbols[i].table], RdiSymbol);
      uint32_t name = symbols[i].table == RDI_TYPE_NODES ? y->a : v->name;
      const uint64_t *g = (const uint64_t *)t.globals.p, *value = (const uint64_t *)t.s[RDI_LOCATION_CONSTANTS].p + 1;
      ok = starts[name + 1] - starts[name] == strlen(symbols[i].name) && memcmp(t.strings.keys.p + starts[name], symbols[i].name, strlen(symbols[i].name)) == 0;
      if (symbols[i].table == RDI_TYPE_NODES) ok = ok && y->kind == 0x2004 && y->byte_size == 4 && y->b == 2;
      else if (symbols[i].table == RDI_CONSTANTS) ok = ok && v->location == (8ull << 56 | 8) && *value == symbols[i].place && v->container_flags == 0 && v->type != 0;
      else ok = ok && v->location == ((symbols[i].table == RDI_GLOBALS ? 6ull : 7ull) << 56 | symbols[i].place) && v->container_flags == symbols[i].flags && (v->type != 0) == (symbols[i].nodes > 1);
      if (symbols[i].table == RDI_GLOBALS) ok = ok && g[0] == symbols[i].place && g[1] == ~(symbols[i].place + symbols[i].covers) && g[2] == 1;
    }
    if (!ok) { printf("symbol %u: wrong variable, constant or typedef\n", i); return 1; }
    Buf *used[] = {&t.s[RDI_GLOBALS], &t.s[RDI_THREAD_VARS], &t.s[RDI_CONSTANTS], &t.s[RDI_TYPE_NODES], &t.s[RDI_LOCATION_CONSTANTS], &t.made, &t.globals, &t.pairs, &t.scratch, &t.strings.keys, &t.strings.at, &t.strings.slots};
    for (unsigned k = 0; k < sizeof used / sizeof *used; k += 1) free(used[k]->p);
    free(a), free(sec);
  }
  /* Local variables: up to three records in a row, each in a block of exactly its length, in the root scope (or, `outside`, in none) of
     a procedure whose type has `params` parameters (255: no type) and returns a 24-byte struct; type 0x1000 is a struct of `big` bytes.
     Wanted: how many locals, then of the last one: its flags, whether it has a type (2: the returned struct), its location (for a set:
     how many cases, and the first and last case's start, end and location), and how many bytes of bytecode were written. */
#define AT(kind, reg, off) ((uint64_t)(kind) << 56 | (uint64_t)(reg) << 48 | (uint64_t)(uint16_t)(off) << 32)
#define REL(off, type, reg) {S_REGREL32, 12, {(uint8_t)(off), (uint8_t)((off) >> 8), (uint8_t)((off) >> 16), (uint8_t)((off) >> 24), (uint8_t)(type), (uint8_t)((type) >> 8), 0, 0, (uint8_t)(reg), (uint8_t)((reg) >> 8), 'x', 0}}
#define LOCAL(flags) {S_LOCAL, 8, {0x74, 0, 0, 0, (uint8_t)(flags), (uint8_t)((flags) >> 8), 'x', 0}}
#define RANGE(sec) 0x10, 0, 0, 0, sec, 0, 0x20, 0   /* 0x20 bytes from offset 0x10 of the section */
#define ALL UINT64_MAX
  static const struct { uint8_t framed, optimised, outside, params, big; uint32_t frame_size, frame_flags; struct { uint16_t kind; uint8_t n, d[28]; } r[3];
                        uint8_t count, flags, typed; uint64_t location; uint8_t cases; uint64_t first[3], last[3]; uint8_t code; } locals[] = {
    /* S_REGREL32: at rsp + 8; a parameter while the procedure's type has that many */
    {0, 0, 0, 2, 0, 0, 0, {REL(8, 0x74, 335)}, 1, 1, 1, AT(3, 5, 8), 0, {0}, {0}, 0}, {0, 0, 0, 2, 0, 0, 0, {REL(8, 0x74, 335), REL(16, 0x74, 335)}, 2, 1, 1, AT(3, 5, 16), 0, {0}, {0}, 0},
    {0, 0, 0, 1, 0, 0, 0, {REL(8, 0x74, 335), REL(16, 0x74, 335)}, 2, 0, 1, AT(3, 5, 16), 0, {0}, {0}, 0}, {0, 0, 0, 0, 0, 0, 0, {REL(8, 0x74, 335)}, 1, 0, 1, AT(3, 5, 8), 0, {0}, {0}, 0},
    {0, 0, 0, 255, 0, 0, 0, {REL(8, 0x74, 335)}, 1, 0, 1, AT(3, 5, 8), 0, {0}, {0}, 0}, {0, 0, 0, 2, 0, 0, 0, {REL(8, 0x74, 329)}, 1, 1, 1, AT(3, 4, 8), 0, {0}, {0}, 0},
    /* not read: in a procedure with S_LOCAL records (but counted), outside a scope (counted too), cut short */
    {0, 1, 0, 2, 0, 0, 0, {REL(8, 0x74, 335)}, 0, 0, 0, 0, 0, {0}, {0}, 0}, {0, 0, 1, 2, 0, 0, 0, {REL(8, 0x74, 335)}, 0, 0, 0, 0, 0, {0}, {0}, 0},
    {0, 0, 0, 2, 0, 0, 0, {{S_REGREL32, 9, {8, 0, 0, 0, 0x74, 0, 0, 0, 0x4f}}}, 0, 0, 0, 0, 0, {0}, {0}, 0},
    {0, 0, 0, 2, 0, 0, 0, {{S_REGREL32, 9, {8, 0, 0, 0, 0x74, 0, 0, 0, 0x4f}}, REL(8, 0x74, 335), REL(16, 0x74, 335)}, 2, 1, 1, AT(3, 5, 16), 0, {0}, {0}, 0},
    /* the offset is signed and fits 16 bits, or becomes bytecode */
    {0, 0, 0, 0, 0, 0, 0, {REL(0xfffffff8, 0x74, 335)}, 1, 0, 1, AT(3, 5, -8), 0, {0}, {0}, 0}, {0, 0, 0, 0, 0, 0, 0, {REL(32767, 0x74, 335)}, 1, 0, 1, AT(3, 5, 32767), 0, {0}, {0}, 0},
    {0, 0, 0, 0, 0, 0, 0, {REL(0xffff8000, 0x74, 335)}, 1, 0, 1, AT(3, 5, -32768), 0, {0}, {0}, 0}, {0, 0, 0, 0, 0, 0, 0, {REL(32768, 0x74, 335)}, 1, 0, 1, 1ull << 56, 0, {0}, {0}, 16},
    {0, 0, 0, 0, 0, 0, 0, {REL(0xffff7fff, 0x74, 335)}, 1, 0, 1, 1ull << 56, 0, {0}, {0}, 16}, {0, 0, 0, 1, 24, 0, 0, {REL(32768, 0x1000, 335)}, 1, 1, 1, 1ull << 56, 0, {0}, {0}, 18},
    /* a parameter that does not fit a register is at the address stored there */
    {0, 0, 0, 1, 24, 0, 0, {REL(8, 0x1000, 335)}, 1, 1, 1, AT(4, 5, 8), 0, {0}, {0}, 0}, {0, 0, 0, 1, 3, 0, 0, {REL(8, 0x1000, 335)}, 1, 1, 1, AT(4, 5, 8), 0, {0}, {0}, 0},
    {0, 0, 0, 1, 16, 0, 0, {REL(8, 0x1000, 335)}, 1, 1, 1, AT(4, 5, 8), 0, {0}, {0}, 0}, {0, 0, 0, 1, 8, 0, 0, {REL(8, 0x1000, 335)}, 1, 1, 1, AT(3, 5, 8), 0, {0}, {0}, 0},
    {0, 0, 0, 1, 9, 0, 0, {REL(8, 0x1000, 335)}, 1, 1, 1, AT(4, 5, 8), 0, {0}, {0}, 0}, {0, 0, 0, 1, 0, 0, 0, {REL(8, 0x1000, 335)}, 1, 1, 1, AT(3, 5, 8), 0, {0}, {0}, 0},
    {0, 0, 0, 0, 24, 0, 0, {REL(8, 0x1000, 335)}, 1, 0, 1, AT(3, 5, 8), 0, {0}, {0}, 0},
    /* without a type, beyond the frame of 0x20 bytes, from rsp or rbp: where the returned struct goes */
    {1, 0, 0, 0, 0, 0x20, 0, {REL(0x40, 0, 335)}, 1, 0, 2, AT(4, 5, 0x40), 0, {0}, {0}, 0}, {1, 0, 0, 0, 0, 0x20, 0, {REL(0x40, 0, 334)}, 1, 0, 2, AT(4, 6, 0x40), 0, {0}, {0}, 0},
    {1, 0, 0, 0, 0, 0x20, 0, {REL(0x21, 0, 335)}, 1, 0, 2, AT(4, 5, 0x21), 0, {0}, {0}, 0}, {1, 0, 0, 0, 0, 0x20, 0, {REL(0x20, 0, 335)}, 1, 0, 0, AT(3, 5, 0x20), 0, {0}, {0}, 0},
    {0, 0, 0, 0, 0, 0x20, 0, {REL(0x40, 0, 335)}, 1, 0, 0, AT(3, 5, 0x40), 0, {0}, {0}, 0}, {1, 0, 0, 0, 0, 0x20, 0, {REL(0x40, 0, 329)}, 1, 0, 0, AT(3, 4, 0x40), 0, {0}, {0}, 0},
    {1, 0, 0, 255, 0, 0x20, 0, {REL(0x40, 0, 335)}, 1, 0, 0, AT(3, 5, 0x40), 0, {0}, {0}, 0}, {1, 0, 0, 0, 0, 0x20, 0, {REL(0x40, 0x74, 335)}, 1, 0, 1, AT(3, 5, 0x40), 0, {0}, {0}, 0},
    /* S_LOCAL and its S_DEFRANGE records: in a register over a range, minus the gaps */
    {0, 1, 0, 0, 0, 0, 0, {LOCAL(1)}, 1, 1, 1, 0, 0, {0}, {0}, 0}, {0, 1, 0, 0, 0, 0, 0, {LOCAL(0)}, 1, 0, 1, 0, 0, {0}, {0}, 0}, {0, 1, 1, 0, 0, 0, 0, {LOCAL(0)}, 0, 0, 0, 0, 0, {0}, {0}, 0},
    {0, 1, 0, 0, 0, 0, 0, {{S_LOCAL, 5, {0x74}}}, 0, 0, 0, 0, 0, {0}, {0}, 0}, {0, 1, 0, 0, 0, 0, 0, {{S_LOCAL, 6, {0x74}}}, 1, 0, 1, 0, 0, {0}, {0}, 0},
    {0, 1, 0, 0, 0, 0, 0, {LOCAL(1), {S_DEFRANGE_REGISTER, 12, {18, 0, 0, 0, RANGE(1)}}}, 1, 1, 1, 0, 1, {0x1010, 0x1030, AT(5, 2, 0)}, {0x1010, 0x1030, AT(5, 2, 0)}, 0},
    {0, 1, 0, 0, 0, 0, 0, {LOCAL(1), {S_DEFRANGE_REGISTER, 11, {18, 0, 0, 0, RANGE(1)}}}, 1, 1, 1, 0, 0, {0}, {0}, 0},
    {0, 1, 0, 0, 0, 0, 0, {LOCAL(1), {S_DEFRANGE_REGISTER, 12, {18, 0, 0, 0, RANGE(2)}}}, 1, 1, 1, 0, 1, {0, 0x20, AT(5, 2, 0)}, {0, 0x20, AT(5, 2, 0)}, 0},   /* a section the image does not have */
    {0, 1, 0, 0, 0, 0, 0, {LOCAL(0), {S_DEFRANGE_REGISTER, 20, {18, 0, 0, 0, RANGE(1), 4, 0, 4, 0, 0x10, 0, 8, 0}}}, 1, 0, 1, 0, 3, {0x1010, 0x1014, AT(5, 2, 0)}, {0x1028, 0x1030, AT(5, 2, 0)}, 0},
    {0, 1, 0, 0, 0, 0, 0, {LOCAL(0), {S_DEFRANGE_REGISTER, 20, {18, 0, 0, 0, RANGE(1), 0, 0, 4, 0, 0x10, 0, 0x10, 0}}}, 1, 0, 1, 0, 1, {0x1014, 0x1020, AT(5, 2, 0)}, {0x1014, 0x1020, AT(5, 2, 0)}, 0},
    {0, 1, 0, 0, 0, 0, 0, {LOCAL(0), {S_DEFRANGE_REGISTER, 19, {18, 0, 0, 0, RANGE(1), 4, 0, 4, 0, 0x10, 0, 8}}}, 1, 0, 1, 0, 2, {0x1010, 0x1014, AT(5, 2, 0)}, {0x1018, 0x1030, AT(5, 2, 0)}, 0},   /* half a gap is none */
    {0, 1, 0, 0, 0, 0, 0, {LOCAL(0), {S_DEFRANGE_REGISTER, 16, {18, 0, 0, 0, 0x10, 0, 0, 0, 1, 0, 0x20, 1, 0xff, 0, 1, 0}}}, 1, 0, 1, 0, 2, {0x1010, 0x110f, AT(5, 2, 0)}, {0x1110, 0x1130, AT(5, 2, 0)}, 0},   /* a gap that starts at 255 in a range of 0x120: not padding */
    {0, 1, 0, 0, 0, 0, 0, {LOCAL(0), {S_DEFRANGE_REGISTER, 12, {18, 0, 0, 0, RANGE(1)}}, {S_DEFRANGE_REGISTER, 12, {0x4f, 1, 0, 0, RANGE(1)}}}, 1, 0, 1, 0, 2, {0x1010, 0x1030, AT(5, 2, 0)}, {0x1010, 0x1030, AT(5, 5, 0)}, 0},
    /* not locals: a global or static in a register; and what ends the run of S_DEFRANGE records */
    {0, 1, 0, 0, 0, 0, 0, {LOCAL(0x200), {S_DEFRANGE_REGISTER, 12, {18, 0, 0, 0, RANGE(1)}}}, 0, 0, 0, 0, 0, {0}, {0}, 0}, {0, 1, 0, 0, 0, 0, 0, {LOCAL(0x400), {S_DEFRANGE_REGISTER, 12, {18, 0, 0, 0, RANGE(1)}}}, 0, 0, 0, 0, 0, {0}, {0}, 0},
    {0, 1, 0, 0, 0, 0, 0, {LOCAL(0), LOCAL(0x200), {S_DEFRANGE_REGISTER, 12, {18, 0, 0, 0, RANGE(1)}}}, 1, 0, 1, 0, 0, {0}, {0}, 0},
    {0, 1, 0, 0, 0, 0, 0, {LOCAL(0), {S_FILESTATIC, 8, {0x74}}, {S_DEFRANGE_REGISTER, 12, {18, 0, 0, 0, RANGE(1)}}}, 1, 0, 1, 0, 0, {0}, {0}, 0},
    {0, 1, 0, 0, 0, 0, 0, {LOCAL(0), {S_END, 0, {0}}, {S_DEFRANGE_REGISTER, 12, {18, 0, 0, 0, RANGE(1)}}}, 1, 0, 1, 0, 0, {0}, {0}, 0},
    {0, 1, 0, 0, 0, 0, 0, {{S_DEFRANGE_REGISTER, 12, {18, 0, 0, 0, RANGE(1)}}}, 0, 0, 0, 0, 0, {0}, {0}, 0},
    {0, 1, 0, 2, 0, 0, 0, {LOCAL(0), REL(8, 0x74, 335), {S_DEFRANGE_REGISTER, 12, {18, 0, 0, 0, RANGE(1)}}}, 1, 0, 1, 0, 1, {0x1010, 0x1030, AT(5, 2, 0)}, {0x1010, 0x1030, AT(5, 2, 0)}, 0},
    /* a part of the variable in a register: only the part at offset 0 */
    {0, 1, 0, 0, 0, 0, 0, {LOCAL(0), {S_DEFRANGE_SUBFIELD_REGISTER, 16, {18, 0, 0, 0, 0, 0, 0, 0, RANGE(1)}}}, 1, 0, 1, 0, 1, {0x1010, 0x1030, AT(5, 2, 0)}, {0x1010, 0x1030, AT(5, 2, 0)}, 0},
    {0, 1, 0, 0, 0, 0, 0, {LOCAL(0), {S_DEFRANGE_SUBFIELD_REGISTER, 16, {18, 0, 0, 0, 4, 0, 0, 0, RANGE(1)}}}, 1, 0, 1, 0, 0, {0}, {0}, 0},
    {0, 1, 0, 0, 0, 0, 0, {LOCAL(0), {S_DEFRANGE_SUBFIELD_REGISTER, 15, {18, 0, 0, 0, 0, 0, 0, 0, RANGE(1)}}}, 1, 0, 1, 0, 0, {0}, {0}, 0},
    /* at register + offset over a range */
    {0, 1, 0, 0, 0, 0, 0, {LOCAL(0), {S_DEFRANGE_REGISTER_REL, 16, {0x4e, 1, 0, 0, 0xf0, 0xff, 0xff, 0xff, RANGE(1)}}}, 1, 0, 1, 0, 1, {0x1010, 0x1030, AT(3, 6, -16)}, {0x1010, 0x1030, AT(3, 6, -16)}, 0},
    {0, 1, 0, 0, 0, 0, 0, {LOCAL(0), {S_DEFRANGE_REGISTER_REL, 15, {0x4e, 1, 0, 0, 0xf0, 0xff, 0xff, 0xff, RANGE(1)}}}, 1, 0, 1, 0, 0, {0}, {0}, 0},
    {0, 1, 0, 0, 0, 0, 0, {LOCAL(0), {S_DEFRANGE_REGISTER_REL, 16, {0x4e, 1, 0, 0, 0, 0, 1, 0, RANGE(1)}}}, 1, 0, 1, 0, 1, {0x1010, 0x1030, 1ull << 56}, {0x1010, 0x1030, 1ull << 56}, 16},
    /* at the frame pointer + offset: S_FRAMEPROC's flags name it for the parameters (bits 16, 17) and for the rest (14, 15): 1 rsp, 2 rbp, 3 r13 */
    {1, 1, 0, 0, 0, 0, 2 << 16 | 1 << 14, {LOCAL(0), {S_DEFRANGE_FRAMEPOINTER_REL_FULL_SCOPE, 4, {0x30, 0, 0, 0}}}, 1, 0, 1, AT(3, 5, 0x30), 0, {0}, {0}, 0},
    {1, 1, 0, 0, 0, 0, 2 << 16 | 1 << 14, {LOCAL(1), {S_DEFRANGE_FRAMEPOINTER_REL_FULL_SCOPE, 4, {0x30, 0, 0, 0}}}, 1, 1, 1, AT(3, 6, 0x30), 0, {0}, {0}, 0},
    {1, 1, 0, 0, 0, 0, 1 << 16 | 3 << 14, {LOCAL(0), {S_DEFRANGE_FRAMEPOINTER_REL_FULL_SCOPE, 4, {0x30, 0, 0, 0}}}, 1, 0, 1, AT(3, 14, 0x30), 0, {0}, {0}, 0},
    {1, 1, 0, 0, 0, 0, 1 << 16, {LOCAL(0), {S_DEFRANGE_FRAMEPOINTER_REL_FULL_SCOPE, 4, {0x30, 0, 0, 0}}}, 1, 0, 1, AT(3, 0, 0x30), 0, {0}, {0}, 0},
    {0, 1, 0, 0, 0, 0, 1 << 14, {LOCAL(0), {S_DEFRANGE_FRAMEPOINTER_REL_FULL_SCOPE, 4, {0x30, 0, 0, 0}}}, 1, 0, 1, 0, 0, {0}, {0}, 0},   /* no S_FRAMEPROC: no frame pointer */
    {1, 1, 0, 0, 0, 0, 1 << 14, {LOCAL(0), {S_DEFRANGE_FRAMEPOINTER_REL_FULL_SCOPE, 3, {0x30, 0, 0}}}, 1, 0, 1, 0, 0, {0}, {0}, 0},
    {1, 1, 0, 0, 0, 0, 1 << 14, {LOCAL(0), {S_DEFRANGE_FRAMEPOINTER_REL, 12, {0x30, 0, 0, 0, RANGE(1)}}}, 1, 0, 1, 0, 1, {0x1010, 0x1030, AT(3, 5, 0x30)}, {0x1010, 0x1030, AT(3, 5, 0x30)}, 0},
    {1, 1, 0, 0, 0, 0, 1 << 14, {LOCAL(0), {S_DEFRANGE_FRAMEPOINTER_REL, 11, {0x30, 0, 0, 0, RANGE(1)}}}, 1, 0, 1, 0, 0, {0}, {0}, 0},
    {0, 1, 0, 0, 0, 0, 1 << 14, {LOCAL(0), {S_DEFRANGE_FRAMEPOINTER_REL, 12, {0x30, 0, 0, 0, RANGE(1)}}}, 1, 0, 1, 0, 0, {0}, {0}, 0},
    /* a place that holds everywhere beside a ranged one: a set of two */
    {1, 1, 0, 0, 0, 0, 1 << 14, {LOCAL(0), {S_DEFRANGE_REGISTER, 12, {18, 0, 0, 0, RANGE(1)}}, {S_DEFRANGE_FRAMEPOINTER_REL_FULL_SCOPE, 4, {0x30, 0, 0, 0}}}, 1, 0, 1, 0, 2, {0x1010, 0x1030, AT(5, 2, 0)}, {0, ALL, AT(3, 5, 0x30)}, 0}};
  for (unsigned i = 0; i < sizeof locals / sizeof *locals; i += 1) {
    uint8_t *sec = memcpy(malloc(sizeof header), header, sizeof header);
    Pdb pdb = {.sec = sec, .sections = 1};
    Out t = {.pointer_size = 8};
    static const int made[] = {RDI_TYPE_NODES, RDI_PROCEDURES, RDI_SCOPES, RDI_LOCALS, RDI_LOCATION_SETS};
    static const uint8_t made_size[] = {sizeof(RdiType), sizeof(RdiSymbol), sizeof(RdiScope), sizeof(RdiSymbol), sizeof(RdiLocationCase)};
    for (unsigned k = 0; k < 5; k += 1) add(&t.s[made[k]], made_size[k]);
    add(&t.made, 4 * (FIRST_RECORD + 1));
    intern(&t.strings, "", 0);
    ((uint32_t *)t.made.p)[FIRST_RECORD] = type_node(&t, (RdiType){.kind = 0x2000, .byte_size = locals[i].big});
    uint32_t returned = type_node(&t, (RdiType){.kind = 0x2000, .byte_size = 24});
    uint32_t type = locals[i].params == 255 ? 0 : type_node(&t, (RdiType){.kind = 0x1005, .byte_size = 8, .a = returned, .b = locals[i].params});
    *(RdiSymbol *)add(&t.s[RDI_PROCEDURES], sizeof(RdiSymbol)) = (RdiSymbol){.type = type};
    *(RdiScope *)add(&t.s[RDI_SCOPES], sizeof(RdiScope)) = (RdiScope){.proc = 1};
    Frame f = {.frame_size = locals[i].frame_size, .frame_flags = locals[i].frame_flags, .framed = locals[i].framed, .optimised = locals[i].optimised};
    for (unsigned k = 0; k < 3 && locals[i].r[k].kind; k += 1) {
      uint8_t *a = memcpy(malloc(locals[i].r[k].n + 1), locals[i].r[k].d, locals[i].r[k].n);
      if (locals[i].r[k].kind == S_END) f.target = 0;   /* as the loop over a module's records does */
      else local_symbol(&t, &pdb, &f, !locals[i].outside, locals[i].r[k].kind, (PdbSpan){a, locals[i].r[k].n});
      free(a);
    }
    locals_by_scope(&t);
    const RdiScope *in = (const RdiScope *)t.s[RDI_SCOPES].p + 1;
    const RdiSymbol *y = (const RdiSymbol *)t.s[RDI_LOCALS].p + in->local_first + in->local_count - (in->local_count != 0);
    const RdiLocationCase *c = (const RdiLocationCase *)t.s[RDI_LOCATION_SETS].p + (y->location & 0xffffffff);
    bool ok = COUNT(t.s[RDI_LOCALS], RdiSymbol) == 1u + locals[i].count && in->local_count == locals[i].count && in->local_first == (locals[i].count != 0) && t.s[RDI_LOCATION_BYTECODE].n == locals[i].code;
    if (ok && locals[i].count) ok = y->flags == locals[i].flags && y->container_flags == 2 && y->container == 1 && y->type == (locals[i].typed == 2 ? returned : y->type) && (y->type != 0) == (locals[i].typed != 0);
    if (ok && locals[i].count && !locals[i].cases) ok = y->location == locals[i].location && COUNT(t.s[RDI_LOCATION_SETS], RdiLocationCase) == 1;
    if (ok && locals[i].cases) ok = y->location >> 32 == (9ull << 24 | locals[i].cases) && COUNT(t.s[RDI_LOCATION_SETS], RdiLocationCase) == 1u + locals[i].cases && (y->location & 0xffffffff) == 1
                                    && memcmp(c, locals[i].first, 24) == 0 && memcmp(c + locals[i].cases - 1, locals[i].last, 24) == 0;
    static const uint8_t far[] = {5, 5, 8, 0, 0, 13, 0, 0x80, 0, 0, 42, 32, 21, 0, 0, 4, 8, 0};   /* read rsp; push 0x8000 as 32 bits, widen; add; read 8 bytes; end */
    if (ok && locals[i].code && locals[i].r[0].d[1] == 0x80) ok = memcmp(t.s[RDI_LOCATION_BYTECODE].p, far, 15) == 0 && t.s[RDI_LOCATION_BYTECODE].p[locals[i].code - 1] == 0 && (locals[i].code == 16 || memcmp(t.s[RDI_LOCATION_BYTECODE].p, far, 18) == 0);
    if (!ok) { printf("local %u: wrong local variable or place\n", i); return 1; }
    Buf *used[] = {&t.s[RDI_TYPE_NODES], &t.s[RDI_PROCEDURES], &t.s[RDI_SCOPES], &t.s[RDI_LOCALS], &t.s[RDI_LOCATION_SETS], &t.s[RDI_LOCATION_BYTECODE], &t.made, &t.cases, &t.strings.keys, &t.strings.at, &t.strings.slots};
    for (unsigned k = 0; k < sizeof used / sizeof *used; k += 1) free(used[k]->p);
    free(sec);
  }
  /* Registers: the first and last code of each run in CV_Reg_X64_XList that the map knows, and the codes around the runs */
  static const uint16_t regs[][2] = {{0, 0}, {1, 1}, {4, 4}, {5, 1}, {8, 4}, {9, 1}, {16, 8}, {17, 1}, {24, 8}, {25, 0}, {153, 0}, {154, 41}, {161, 48}, {162, 41}, {165, 41}, {166, 42},
    {193, 48}, {194, 41}, {201, 48}, {202, 41}, {209, 48}, {210, 0}, {251, 0}, {252, 49}, {259, 56}, {260, 49}, {263, 49}, {264, 50}, {291, 56}, {292, 49}, {299, 56}, {300, 49}, {307, 56}, {308, 0},
    {323, 0}, {324, 7}, {325, 8}, {326, 6}, {327, 5}, {328, 1}, {329, 4}, {330, 2}, {331, 3}, {332, 7}, {333, 8}, {334, 6}, {335, 5}, {336, 9}, {343, 16}, {344, 9}, {367, 16}, {368, 0}, {30006, 0}};
  for (unsigned i = 0; i < sizeof regs / sizeof *regs; i += 1)
    if (reg_code(regs[i][0]) != regs[i][1]) { printf("register %u: wrong code\n", regs[i][0]); return 1; }
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
