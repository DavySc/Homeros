#include "conv.h"
#include "le.h"
#include "pdb.h"
#include "rdi.h"
#include <stdlib.h>

/* ---- growing arrays ---- */

typedef struct { uint8_t *p; uint64_t n, cap; } Buf;

static void *add(Buf *b, uint64_t n)   /* n more zeroed bytes */
{
  if (b->n + n >= b->cap) {
    b->cap = (b->n + n) * 2 + 64;
    b->p = realloc(b->p, b->cap);
    if (!b->p) abort();
  }
  void *at = memset(b->p + b->n, 0, n);
  b->n += n;
  return at;
}
#define COUNT(buf, T) ((uint32_t)((buf).n / sizeof(T)))
#define LAST(buf, T) ((T *)((buf).p + (buf).n) - 1)

/* Everything that is sorted is a record of four numbers compared in order, so there is one
   comparison function. */
static void rec(Buf *b, uint64_t k0, uint64_t k1, uint64_t k2, uint64_t k3)
{
  uint64_t *r = add(b, 32);
  r[0] = k0, r[1] = k1, r[2] = k2, r[3] = k3;
}
static int rec_cmp(const void *a, const void *b)
{
  const uint64_t *x = a, *y = b;
  for (int i = 0; i < 4; i += 1) if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
  return 0;
}
/* a name map entry: `index` of a table is filed under a string; nothing is filed under the empty string */
static void named(Buf *pairs, uint64_t kind, uint32_t string, uint64_t index) { if (string) rec(pairs, kind, string, index, 0); }
static uint64_t *sorted(Buf *b, uint64_t *count)
{
  *count = b->n / 32;
  if (*count) qsort(b->p, *count, 32, rec_cmp);
  return (uint64_t *)b->p;
}

/* ---- a set of byte strings numbered in order of arrival: the string table, the path tree's
   (parent, name) pairs and the source files are each one of these ---- */

typedef struct { Buf keys, at, slots; } Names;   /* at: where each key starts, then the end */

static uint32_t *names_slot(Names *m, const void *key, uint32_t len)   /* the key's slot, or the empty one it belongs in */
{
  uint32_t *slots = (uint32_t *)m->slots.p, *at = (uint32_t *)m->at.p;
  for (uint64_t n = m->slots.n / 4, i = rdi_hash(key, len) % n;; i = (i + 1) % n) {
    uint32_t e = slots[i] - 1;
    if (slots[i] == 0 || (at[e + 1] - at[e] == len && (len == 0 || memcmp(m->keys.p + at[e], key, len) == 0))) return slots + i;
  }
}

static uint32_t intern(Names *m, const void *key, uint32_t len)
{
  if (!m->at.n) add(&m->at, 4), add(&m->keys, 0);     /* keys.p is never null, even when every key is empty */
  uint32_t count = COUNT(m->at, uint32_t) - 1, *at = (uint32_t *)m->at.p;
  if (m->slots.n < (uint64_t)(count + 1) * 8) {       /* at most half full */
    uint64_t size = m->slots.n ? m->slots.n * 2 : 1024;
    m->slots.n = 0;
    add(&m->slots, size);
    for (uint32_t e = 0; e < count; e += 1) *names_slot(m, m->keys.p + at[e], at[e + 1] - at[e]) = e + 1;
  }
  uint32_t *slot = names_slot(m, key, len);
  if (*slot) return *slot - 1;
  *slot = count + 1;
  if (len) memcpy(add(&m->keys, len), key, len);
  *(uint32_t *)add(&m->at, 4) = (uint32_t)m->keys.n;
  return count;
}

/* ---- the output ---- */

enum { S_END = 0x0006, S_THUNK32 = 0x1102, S_BLOCK32 = 0x1103, S_CONSTANT = 0x1107, S_UDT = 0x1108, S_LDATA32 = 0x110c, S_GDATA32 = 0x110d,
       S_PUB32 = 0x110e, S_LTHREAD32 = 0x1112, S_GTHREAD32 = 0x1113, S_LPROC32 = 0x110f, S_GPROC32 = 0x1110,
       S_INLINESITE = 0x114d, S_INLINESITE_END = 0x114e, S_FRAMEPROC = 0x1012, S_REGREL32 = 0x1111, S_LOCAL = 0x113e,
       S_DEFRANGE_REGISTER = 0x1141, S_DEFRANGE_FRAMEPOINTER_REL, S_DEFRANGE_SUBFIELD_REGISTER, S_DEFRANGE_FRAMEPOINTER_REL_FULL_SCOPE,
       S_DEFRANGE_REGISTER_REL, S_FILESTATIC = 0x1153,   /* CV_SymKind */
       LF_MODIFIER = 0x1001, LF_POINTER = 0x1002, LF_PROCEDURE = 0x1008, LF_MFUNCTION = 0x1009, LF_ARGLIST = 0x1201,
       LF_BITFIELD = 0x1205, LF_FIELDLIST = 0x1203, LF_METHODLIST = 0x1206, LF_BCLASS = 0x1400, LF_VBCLASS = 0x1401, LF_IVBCLASS = 0x1402,
       LF_INDEX = 0x1404, LF_VFUNCTAB = 0x1409, LF_ENUMERATE = 0x1502, LF_MEMBER = 0x150d, LF_STMEMBER = 0x150e, LF_METHOD = 0x150f,
       LF_NESTTYPE = 0x1510, LF_ONEMETHOD = 0x1511, LF_NESTTYPEEX = 0x1512, LF_ARRAY = 0x1503, LF_CLASS = 0x1504, LF_STRUCTURE = 0x1505, LF_UNION = 0x1506, LF_ENUM = 0x1507,
       LF_FUNC_ID = 0x1601, LF_MFUNC_ID = 0x1602 };                                 /* CV_LeafKind */

typedef struct {
  Buf s[RDI_SECTIONS];
  Names strings, nodes, files;
  Buf pairs;                 /* name map entries: (kind, string, index) */
  Buf scopes;                /* what each scope covers: (first, ~end, scope) */
  Buf globals;               /* what each global variable covers, the same way */
  Buf cases;                 /* where local variables live: (first, end, location), each local's together */
  /* types: the PDB's type records and where each starts; per type id, 1 + the index of the type
     made for it (0: not made); the full definitions by name; scratch for a key or a parameter list */
  PdbSpan leaves;
  uint32_t pointer_size;
  Buf leaf_at, made, scratch, defined_id;
  Names defined;
} Out;

static bool slash(uint8_t c) { return c == '/' || c == '\\'; }

/* The path's node in the tree of path parts, made if new. Parts split at either slash, a
   leading '/' stays on the first part, "." stays and ".." goes up (rdim_bake_path_tree_insert;
   that function never returns for a path that starts with a backslash, this one skips it).
   A relative path starts from `node`. */
static uint32_t path_node(Out *o, uint32_t node, PdbSpan path)
{
  for (uint32_t i = 0; i < path.n;) {
    uint32_t start = i;
    if (slash(path.p[i])) {
      i += 1;
      if (start > 0 || path.p[0] == '\\') continue;
    }
    while (i < path.n && !slash(path.p[i])) i += 1;
    uint32_t len = i - start;
    const uint8_t *part = path.p + start;
    if (len == 1 && part[0] == '.') continue;
    if (len == 2 && part[0] == '.' && part[1] == '.' && node != 0) {
      node = ((RdiPathNode *)o->s[RDI_FILE_PATH_NODES].p)[node].parent;
      continue;
    }
    uint32_t key[2] = {node, intern(&o->strings, part, len)};
    uint32_t child = intern(&o->nodes, key, sizeof key);
    if (child == COUNT(o->s[RDI_FILE_PATH_NODES], RdiPathNode)) *(RdiPathNode *)add(&o->s[RDI_FILE_PATH_NODES], sizeof(RdiPathNode)) = (RdiPathNode){.name = key[1], .parent = node};
    node = child;
  }
  return node;
}

/* the source file at a path, made if new; a file keeps the first checksum seen for it */
static uint32_t source_file(Out *o, const PdbLineBlock *b, uint32_t folder)
{
  static const uint8_t sum_size[4] = {0, 16, 20, 32};
  static const int sum_section[4] = {0, RDI_MD5, RDI_SHA1, RDI_SHA256};
  /* a relative path is taken from the folder of the unit's object file (rdi_from_codeview.c, PathStyle_Relative) */
  bool rooted = b->file.n == 0 || slash(b->file.p[0]) || (b->file.n > 1 && b->file.p[1] == ':');
  uint32_t node = path_node(o, rooted ? 0 : folder, b->file), file = intern(&o->files, &node, sizeof node);
  if (file == COUNT(o->s[RDI_SOURCE_FILES], RdiSourceFile)) {
    *(RdiSourceFile *)add(&o->s[RDI_SOURCE_FILES], sizeof(RdiSourceFile)) = (RdiSourceFile){.path_node = node, .line_map = file};
    ((RdiPathNode *)o->s[RDI_FILE_PATH_NODES].p)[node].source_file = file;
  }
  RdiSourceFile *f = (RdiSourceFile *)o->s[RDI_SOURCE_FILES].p + file;
  if (file != 0 && f->checksum_kind == 0 && b->sum_kind < 4 && b->sum.n != 0 && b->sum.n == sum_size[b->sum_kind]) {
    Buf *sums = &o->s[sum_section[b->sum_kind]];
    if (!sums->n) add(sums, b->sum.n);                 /* index 0 is no checksum */
    f->checksum_kind = b->sum_kind;
    f->checksum = (uint32_t)(sums->n / b->sum.n);
    memcpy(add(sums, b->sum.n), b->sum.p, b->sum.n);
  }
  return file;
}

static void pair(Buf *b, uint64_t first, uint64_t opl)
{
  uint64_t *v = add(b, 16);
  v[0] = first, v[1] = opl;
}

/* A scope of `proc` inside `parent`. It covers the pairs [first, opl) written to the scope
   offsets since `start`; a pair without an address or without bytes is dropped. */
static uint32_t scope(Out *o, uint32_t proc, uint32_t parent, uint32_t inline_site, uint64_t start)
{
  Buf *v = &o->s[RDI_SCOPE_VOFFS];
  uint32_t idx = COUNT(o->s[RDI_SCOPES], RdiScope);
  uint64_t *p = (uint64_t *)v->p, kept = start / 8;
  for (uint64_t i = start / 8; i < v->n / 8; i += 2) {
    if (p[i] == 0 || p[i] >= p[i + 1]) continue;
    rec(&o->scopes, p[i], ~p[i + 1], idx, 0);
    p[kept] = p[i], p[kept + 1] = p[i + 1], kept += 2;
  }
  v->n = kept * 8;
  *(RdiScope *)add(&o->s[RDI_SCOPES], sizeof(RdiScope)) = (RdiScope){.proc = proc, .parent = parent, .voff_first = (uint32_t)(start / 8), .voff_opl = (uint32_t)kept, .inline_site = inline_site};
  return idx;
}

/* One operand of an inline site's annotations: 1, 2 or 4 bytes, by the top bits of the first
   (cv_decode_inline_annot_u32). False when it is cut short or malformed. */
static bool operand(PdbSpan a, uint32_t *at, uint32_t *value)
{
  uint8_t first = *at < a.n ? a.p[*at] : 0xff;
  uint32_t size = first < 0x80 ? 1 : first < 0xc0 ? 2 : first < 0xe0 ? 4 : 0;
  if (size == 0 || size > a.n - *at) return false;
  *value = first & (size == 4 ? 0x1f : 0x7f);
  for (uint32_t i = 1; i < size; i += 1) *value = *value << 8 | a.p[*at + i];
  *at += size;
  return true;
}

/* a signed operand of an annotation: the low bit is the sign (cv_inline_annot_signed_from_unsigned_operand) */
static uint32_t step(uint32_t v) { return v & 1 ? 0 - (v >> 1) : v >> 1; }

/* What an inlined call covers, as pairs added to `out`, from the annotations of its
   S_INLINESITE record; offsets count from the procedure's first byte, `base`. An annotation
   moves the code offset (a line starts there), or gives the length of the code at the offset,
   which ends a piece; a piece runs from the first offset since the last piece ended, and one
   that starts where the piece before ended continues it. Offsets left without a length are a
   piece up to the last of them. This is how LLVM reads and writes them (llvm-pdbutil's "code"
   and "code end", MCCodeView.cpp). RAD (cv_c13_inline_site_decoder_step) also adds the length
   of annotation 12 to the offset, and sibling calls then overlap (lessons, L26). `lines`, which
   is this call's alone, gets a line record at each new offset and an end record at each end of
   a piece, as a unit's lines have; the file is still the offset of its checksum, and the first
   are the `file` and `line` the function starts at. Nothing is
   added when the annotations are malformed or of a kind not decoded. NOT YET decoded: code
   moved out of the procedure (annotation 2), an offset that is not a step (annotation 1) and
   S_INLINESITE2; no compiler here writes them, and LLVM and RAD read the first two differently. */
static void inline_pieces(Buf *out, Buf *lines, PdbSpan a, uint64_t base, uint32_t file, uint32_t line)
{
  uint64_t start = out->n;
  uint32_t off = 0, lo = 0;
  bool open = false;                                     /* an offset was given since the last piece ended: `lo` */
  for (uint32_t at = 0, op = 1, x, y; op != 0;) {
    uint64_t end = 0;                                    /* where a piece ends, when this annotation ends one */
    x = y = op = 0;                                      /* the end of the record is annotation 0, the end */
    bool ok = at == a.n || (operand(a, &at, &op) && (op == 0 || operand(a, &at, &x)) && (op != 12 || operand(a, &at, &y)));
    if (op == 3) off += x;                               /* ChangeCodeOffset */
    else if (op == 11) off += x & 15, line += step(x >> 4);   /* ChangeCodeOffsetAndLineOffset */
    else if (op == 12) off += y;                         /* ChangeCodeLengthAndCodeOffset */
    else if (op == 5) file = x;                          /* ChangeFile */
    else if (op == 6) line += step(x);                   /* ChangeLineOffset */
    else if (op != 0 && op != 4) ok = false;             /* ChangeCodeLength moves nothing yet */
    if (!ok) {
      out->n = start, lines->n = 0;
      return;
    }
    if (op == 3 || op == 11 || op == 12) rec(lines, base + off, 1ull << 63 | lines->n, file, line);   /* a line starts at each new offset */
    if (!open && op != 0 && op != 5 && op != 6) lo = off, open = true;
    if (op == 4 || op == 12) end = base + off + x;
    else if (op == 0 && open) end = base + off;
    if (op == 4) off += x;                               /* the next offset counts from this end; after annotation 12 it does not */
    if (end == 0) continue;
    if (out->n > start && *LAST(*out, uint64_t) == base + lo) *LAST(*out, uint64_t) = end;
    else pair(out, base + lo, end);
    rec(lines, end, lines->n, 0, 0);
    open = false;
  }
}

/* ---- types (rdi_from_codeview.c, "types pass 1" to "types pass 4") ---- */

enum { FIRST_RECORD = 0x1000 };   /* the id of the first type record; the ids below are the basic types */

/* a number in a type record: 2 bytes, which are the value when below 0x8000 and otherwise say how many bytes follow (cv_numeric_from_data_range) */
static uint64_t numeric(PdbSpan d, uint32_t *at)
{
  static const uint8_t size[11] = {1, 2, 2, 4, 4, 0, 0, 0, 0, 8, 8};   /* LF_CHAR, SHORT, USHORT, LONG, ULONG, .., QUADWORD, UQUADWORD */
  uint64_t v = d.n - *at >= 2 ? le16(d.p + *at) : 0, n = v >= 0x8000 && v < 0x800b ? size[v - 0x8000] : 0;
  *at += d.n - *at >= 2 ? 2 : 0;
  if (v < 0x8000 || d.n - *at < n) return v < 0x8000 ? v : 0;
  bool negative = n != 0 && (v == 0x8000 || v == 0x8001 || v == 0x8003) && d.p[*at + n - 1] >= 0x80;   /* the signed kinds shorter than 8 bytes, widened as cv_u64_from_numeric does */
  v = negative ? UINT64_MAX : 0;
  for (uint64_t i = n; i > 0; i -= 1) v = v << 8 | d.p[*at + i - 1];
  *at += (uint32_t)n;
  return v;
}

/* the type record of an id: its kind and bytes; false when there is none */
static bool leaf(const Out *o, uint32_t id, uint16_t *kind, PdbSpan *d)
{
  uint32_t i = id - FIRST_RECORD;
  return i < COUNT(o->leaf_at, uint32_t) && pdb_symbol(o->leaves, &(uint32_t){((uint32_t *)o->leaf_at.p)[i]}, kind, d);
}

/* A struct, class, union or enum record: where its flags are, and its name. False for another kind. */
static bool named_leaf(uint16_t kind, PdbSpan d, uint16_t *props, uint64_t *size, PdbSpan *name)
{
  bool record = kind == LF_CLASS || kind == LF_STRUCTURE;   /* CV_LeafStruct: count, props, 3 ids, size, name; CV_LeafUnion: count, props, id, size, name */
  uint32_t at = record ? 16 : kind == LF_UNION ? 8 : 12;    /* CV_LeafEnum: count, props, 2 ids, name */
  if ((!record && kind != LF_UNION && kind != LF_ENUM) || d.n < at) return false;
  *props = le16(d.p + 2);
  *size = kind == LF_ENUM ? 0 : numeric(d, &at);
  *name = pdb_string(d, &at);
  return true;
}

/* The id of the first full definition filed under a name; 0 when there is none. With `id`,
   files that definition unless one is there. RAD looks a scoped type up by its unique name,
   which follows the name in the record; in no test file does that find another definition. */
static uint32_t defined(Out *o, PdbSpan name, uint32_t id)
{
  if (id && intern(&o->defined, name.p, name.n) == COUNT(o->defined_id, uint32_t)) *(uint32_t *)add(&o->defined_id, 4) = id;
  uint32_t slot = o->defined.slots.n ? *names_slot(&o->defined, name.p, name.n) : 0;
  return slot ? ((uint32_t *)o->defined_id.p)[slot - 1] : 0;
}

static uint32_t type_node(Out *o, RdiType y)
{
  *(RdiType *)add(&o->s[RDI_TYPE_NODES], sizeof y) = y;
  return COUNT(o->s[RDI_TYPE_NODES], RdiType) - 1;
}

#define SIZE_OF(type) (((RdiType *)o->s[RDI_TYPE_NODES].p)[type].byte_size)

/* The type made for a type id; 0 when there is none. A basic type (an id below the first
   record's: its low byte, or with bits 8 to 11 a pointer to that) is made when first asked
   for. A record's is looked up, never made here: `record_type` makes them in id order, so a
   record that names a later one, or itself, has no type there, and nothing recurses however
   a damaged file chains its records. RAD's `int32` and `uint32` are `int` and `unsigned int`. */
static uint32_t type_of(Out *o, uint32_t id)
{
  /* CV_BasicType, its C name, and the built-in type it is an alias of with that type's size (the table in "types pass 4") */
  static const struct { uint8_t cv, kind, size; const char *name, *built_in; } basic[] = {
    {0x10, 0x04, 1, "signed char", "Char8"}, {0x11, 0x12, 2, "short", "S16"}, {0x12, 0x13, 4, "long", "S32"}, {0x13, 0x14, 8, "long long", "S64"},
    {0x20, 0x07, 1, "unsigned char", "UChar8"}, {0x21, 0x0b, 2, "unsigned short", "U16"}, {0x22, 0x0c, 4, "unsigned long", "U32"},
    {0x23, 0x0d, 8, "unsigned long long", "U64"}, {0x30, 0x11, 1, "bool", "S8"}, {0x40, 0x1b, 4, "float", "F32"}, {0x41, 0x1e, 8, "double", "F64"},
    {0x70, 0x04, 1, "char", "Char8"}, {0x71, 0x08, 2, "wchar_t", "UChar16"}, {0x74, 0x13, 4, "int", "S32"}, {0x75, 0x0c, 4, "unsigned int", "U32"},
    {0x76, 0x14, 8, "__int64", "S64"}, {0x77, 0x0d, 8, "__uint64", "U64"}, {0x7a, 0x05, 2, "char16_t", "Char16"}, {0x7b, 0x06, 4, "char32_t", "Char32"},
    {0x03, 0x01, 0, 0, "Void"}, {0x08, 0x03, 4, 0, "HResult"}};
  if (id >= COUNT(o->made, uint32_t)) return 0;
  uint32_t made = ((uint32_t *)o->made.p)[id];
  if (made || id >= FIRST_RECORD) return made;
  if (id >> 8 != 0) {
    made = type_of(o, id & 0xff);
    made = type_node(o, (RdiType){.kind = 0x1001, .byte_size = o->pointer_size, .a = made});
  } else for (unsigned i = 0; i < sizeof basic / sizeof *basic; i += 1) {
    if (basic[i].cv != id) continue;
    made = type_node(o, (RdiType){.kind = basic[i].kind, .byte_size = basic[i].size, .a = intern(&o->strings, basic[i].built_in, (uint32_t)strlen(basic[i].built_in))});
    if (basic[i].name) made = type_node(o, (RdiType){.kind = 0x2004, .byte_size = basic[i].size, .a = intern(&o->strings, basic[i].name, (uint32_t)strlen(basic[i].name)), .b = made});
  }
  return ((uint32_t *)o->made.p)[id] = made;
}

/* Makes the type of a type record. NOT YET made, so no type: pointers to members (RAD makes
   them plain pointers, which they are not), LF_ALIAS, the newer struct records. */
static void record_type(Out *o, uint32_t id)
{
  uint32_t made = 0, direct;
  uint16_t kind, props;
  uint64_t size;
  PdbSpan d, name;
  if (!leaf(o, id, &kind, &d)) return;
  if (kind == LF_MODIFIER && d.n >= 6) {                   /* CV_LeafModifier: id, flags (const, volatile) */
    direct = made = type_of(o, le32(d.p));
    if (le16(d.p + 4) & 3) made = type_node(o, (RdiType){.kind = 0x1000, .flags = le16(d.p + 4) & 3, .byte_size = SIZE_OF(direct), .a = direct});
  } else if (kind == LF_POINTER && d.n >= 8 && ((le32(d.p + 4) >> 5 & 7) < 2 || (le32(d.p + 4) >> 5 & 7) == 4)) {
    /* CV_LeafPointer: id, attributes. The mode (CV_PointerMode) says pointer, reference or rvalue reference. */
    uint32_t attribs = le32(d.p + 4), mode = attribs >> 5 & 7;
    direct = type_of(o, le32(d.p));
    made = type_node(o, (RdiType){.kind = mode == 1 ? 0x1002 : mode == 4 ? 0x1003 : 0x1001, .byte_size = o->pointer_size, .a = direct});
    uint16_t flags = (attribs >> 10 & 1) | (attribs >> 9 & 1) << 1 | (attribs >> 12 & 1) << 2;   /* const, volatile, restrict of the pointer itself */
    if (flags) made = type_node(o, (RdiType){.kind = 0x1000, .flags = flags, .byte_size = o->pointer_size, .a = made});
  } else if ((kind == LF_PROCEDURE && d.n >= 12) || (kind == LF_MFUNCTION && d.n >= 20)) {
    /* CV_LeafProcedure: return type, 4 bytes, argument list. CV_LeafMFunction: return type, class, this, 4 bytes, argument list.
       A method's first parameter is its `this`. An id of 0 in the list is the `...` and no parameter (RAD keeps it for a method). */
    bool plain = kind == LF_PROCEDURE, method = !plain && le32(d.p + 8) != 0;
    uint32_t first = COUNT(o->s[RDI_INDEX_RUNS], uint32_t), param;
    PdbSpan args;
    direct = type_of(o, le32(d.p));
    if (method) param = type_of(o, le32(d.p + 8)), *(uint32_t *)add(&o->s[RDI_INDEX_RUNS], 4) = param;
    if (leaf(o, le32(d.p + (plain ? 8 : 16)), &kind, &args) && kind == LF_ARGLIST && args.n >= 4)
      for (uint32_t i = 0; i < le32(args.p) && args.n - 4 >= 4 * (uint64_t)(i + 1); i += 1)
        if (le32(args.p + 4 + 4 * i) != 0) param = type_of(o, le32(args.p + 4 + 4 * i)), *(uint32_t *)add(&o->s[RDI_INDEX_RUNS], 4) = param;
    made = type_node(o, (RdiType){.kind = method ? 0x1006 : 0x1005, .byte_size = o->pointer_size, .a = direct, .b = COUNT(o->s[RDI_INDEX_RUNS], uint32_t) - first, .c = first});
  } else if (kind == LF_BITFIELD && d.n >= 6) {            /* CV_LeafBitField: id, length, position */
    direct = type_of(o, le32(d.p));
    made = type_node(o, (RdiType){.kind = 0xf000, .byte_size = SIZE_OF(direct), .a = direct, .b = d.p[5], .c = d.p[4]});
  } else if (kind == LF_ARRAY && d.n >= 8) {               /* CV_LeafArray: element type, index type, size in bytes */
    direct = type_of(o, le32(d.p));
    size = numeric(d, &(uint32_t){8});
    made = type_node(o, (RdiType){.kind = 0x1004, .byte_size = (uint32_t)size, .a = direct, .b = SIZE_OF(direct) ? (uint32_t)(size / SIZE_OF(direct)) : 0});
  } else if (named_leaf(kind, d, &props, &size, &name)) {
    bool forward = props >> 7 & 1;                         /* CV_TypeProp: forward reference */
    /* A forward reference means the first full definition of its name, made now if it comes
       later (it is no forward reference, so this goes one deep); without one it is a type with only a name. */
    uint32_t full = forward ? defined(o, name, 0) : 0;
    if (full && !type_of(o, full)) record_type(o, full);
    direct = !forward && kind == LF_ENUM ? type_of(o, le32(d.p + 4)) : 0;
    made = full ? type_of(o, full)
                : type_node(o, (RdiType){.kind = forward ? (kind == LF_STRUCTURE ? 0x2005 : kind == LF_UNION ? 0x2006 : kind == LF_CLASS ? 0x2007 : 0x2008)
                                                         : (kind == LF_STRUCTURE ? 0x2000 : kind == LF_CLASS ? 0x2001 : kind == LF_UNION ? 0x2002 : 0x2003),
                                         .byte_size = kind == LF_ENUM ? SIZE_OF(direct) : (uint32_t)size, .a = intern(&o->strings, name.p, name.n), .b = direct});
  }
  ((uint32_t *)o->made.p)[id] = made;
}

static void member(Out *o, uint16_t kind, PdbSpan name, uint32_t type_id, uint64_t off)
{
  *(RdiMember *)add(&o->s[RDI_MEMBERS], sizeof(RdiMember)) = (RdiMember){.kind = kind, .name = intern(&o->strings, name.p, name.n), .type = type_of(o, type_id), .off = (uint32_t)off};
}

/* Gives the full struct, class, union or enum record of an id its record of members, from its
   field list ("gather this lane's UDTs"). A field is its kind, 2 bytes (attributes, or a count),
   mostly a type id, then what its kind says, padded to 4 bytes; so every field takes at least 8.
   A long list ends in an LF_INDEX naming the list that continues it, which compilers write
   before it: only an earlier one is followed, so a damaged file cannot loop. A kind not known
   here ends the list, as does an enum's field in a struct or the reverse. RAD does not step
   over the two numbers of a virtual base, which ends its list there; here they are stepped over. */
static void members(Out *o, uint32_t id)
{
  /* a method's kind by CV_MethodProp: plain, virtual, static, friend, new virtual, virtual without a body, new and without a body */
  static const uint16_t method_kind[8] = {0x100, 0x102, 0x101, 0x100, 0x102, 0x102, 0x102, 0x100};
  static const uint8_t slot_bytes[8] = {0, 0, 0, 0, 4, 0, 4, 0};   /* a new virtual method says where its slot is */
  uint16_t kind, props;
  uint64_t size;
  PdbSpan d, name, list, methods;
  if (!leaf(o, id, &kind, &d) || !named_leaf(kind, d, &props, &size, &name) || props >> 7 & 1) return;
  bool values = kind == LF_ENUM;
  Buf *to = &o->s[values ? RDI_ENUM_MEMBERS : RDI_MEMBERS];
  size_t each = values ? sizeof(RdiEnumMember) : sizeof(RdiMember);
  uint32_t first = (uint32_t)(to->n / each), list_id = le32(d.p + (values ? 8 : 4)), at = 0;
  while (leaf(o, list_id, &kind, &list) && kind == LF_FIELDLIST && at <= list.n && list.n - at >= 8) {
    uint16_t field = le16(list.p + at), prop = le16(list.p + at + 2) >> 2 & 7;
    uint32_t type_id = le32(list.p + at + 4), next = at + 8;
    if (field == LF_INDEX) {
      if (type_id >= list_id) break;
      list_id = type_id, at = 0;
      continue;
    }
    if (values != (field == LF_ENUMERATE)) break;
    if (field == LF_ENUMERATE) {                           /* CV_LeafEnumerate: attributes, value, name */
      next = at + 4;
      uint64_t value = numeric(list, &next);
      name = pdb_string(list, &next);
      *(RdiEnumMember *)add(to, sizeof(RdiEnumMember)) = (RdiEnumMember){.name = intern(&o->strings, name.p, name.n), .val = value};
    } else if (field == LF_MEMBER || field == LF_BCLASS) { /* CV_LeafMember: attributes, type, offset, name; CV_LeafBClass has no name */
      uint64_t off = numeric(list, &next);
      member(o, field == LF_MEMBER ? 1 : 0x201, field == LF_MEMBER ? pdb_string(list, &next) : (PdbSpan){0, 0}, type_id, off);
    } else if (field == LF_STMEMBER || field == LF_NESTTYPE || field == LF_NESTTYPEEX) {   /* attributes or nothing, type, name */
      member(o, field == LF_STMEMBER ? 2 : 0x300, pdb_string(list, &next), type_id, 0);
    } else if (field == LF_ONEMETHOD) {                    /* CV_LeafOneMethod: attributes, type, for a new virtual method its slot's offset, name */
      next += slot_bytes[prop];
      member(o, method_kind[prop], pdb_string(list, &next), type_id, 0);
    } else if (field == LF_METHOD) {                       /* CV_LeafMethod: count, method list, name; the list: attributes, 2 bytes, type, maybe the offset */
      name = pdb_string(list, &next);
      if (!leaf(o, type_id, &kind, &methods) || kind != LF_METHODLIST) methods.n = 0;
      for (uint32_t m = 0; m <= methods.n && methods.n - m >= 8; m += 8 + slot_bytes[prop]) {
        prop = le16(methods.p + m) >> 2 & 7;
        member(o, method_kind[prop], name, le32(methods.p + m + 4), 0);
      }
    } else if (field == LF_VBCLASS || field == LF_IVBCLASS) {   /* CV_LeafVBClass: attributes, type, the pointer's type, two numbers */
      next += 4;
      numeric(list, &next), numeric(list, &next);
      member(o, 0x202, (PdbSpan){0, 0}, type_id, 0);
    } else if (field != LF_VFUNCTAB) break;                /* CV_LeafVFuncTab: 2 bytes, type: no member */
    at = (next + 3) / 4 * 4;
  }
  uint32_t self = type_of(o, id), count = (uint32_t)(to->n / each) - first;
  ((RdiType *)o->s[RDI_TYPE_NODES].p)[self].c = COUNT(o->s[RDI_UDTS], RdiUdt);
  *(RdiUdt *)add(&o->s[RDI_UDTS], sizeof(RdiUdt)) = (RdiUdt){.self_type = self, .flags = values && count, .member_first = first, .member_count = count};
}

/* A variable, constant or typedef symbol, in the global stream or a module's ("symbols pass 2").
   A place is one 64-bit location (RDI_Location): its kind in the top byte (6 an offset in the
   image, 7 in the thread's block, 8 in the constants' data), the offset below. A constant's
   value is 8 bytes there, and its name gets the containers of its type's name in front
   (cv2r_end_of_cplusplus_container_name: up to the last "::" outside angle brackets), which
   is how an enumerator of `a::E` becomes `a::name`. A typedef is read from the global stream
   only. NOT made: RAD's variable without type or place for every public symbol that is not
   code, through which nothing can be read. */
static void data_symbol(Out *o, const Pdb *pdb, uint16_t kind, PdbSpan d, bool global_stream)
{
  bool data = kind == S_LDATA32 || kind == S_GDATA32, thread = kind == S_LTHREAD32 || kind == S_GTHREAD32;
  if (d.n < (data || thread ? 10 : 4) || !(data || thread || kind == S_CONSTANT || (kind == S_UDT && global_stream))) return;
  uint32_t type = type_of(o, le32(d.p)), at = 10;
  const RdiType *y = (const RdiType *)o->s[RDI_TYPE_NODES].p + type;
  if (data || thread) {                     /* CV_SymData32, CV_SymThread32: type, offset, section, name */
    PdbSpan name = pdb_string(d, &at);
    uint64_t off = data ? pdb_voff(pdb, le16(d.p + 8), le32(d.p + 4)) : le32(d.p + 4);
    Buf *to = &o->s[data ? RDI_GLOBALS : RDI_THREAD_VARS];
    RdiSymbol v = {.name = intern(&o->strings, name.p, name.n), .container_flags = kind == S_GDATA32 || kind == S_GTHREAD32 ? 0x80 : 0, .location = (uint64_t)(data ? 6 : 7) << 56 | off, .type = type};
    named(&o->pairs, data ? RDI_NAMES_GLOBALS : RDI_NAMES_THREAD_VARS, v.name, COUNT(*to, RdiSymbol));
    if (data) rec(&o->globals, off, ~(off + y->byte_size), COUNT(*to, RdiSymbol), 0);   /* "fill records" of the global map: one without a type covers nothing */
    *(RdiSymbol *)add(to, sizeof v) = v;
  } else if (kind == S_CONSTANT) {                         /* CV_SymConstant: type, value, name */
    at = 4;
    uint64_t value = numeric(d, &at);
    PdbSpan name = pdb_string(d, &at);
    const uint32_t *starts = (const uint32_t *)o->strings.at.p;
    const uint8_t *of = o->strings.keys.p + starts[y->kind >= 0x2000 && y->kind < 0xf000 ? y->a : 0];
    uint32_t end = y->kind >= 0x2000 && y->kind < 0xf000 ? starts[y->a + 1] - starts[y->a] : 0, depth = 0;
    for (; end >= 2 && (depth != 0 || of[end - 2] != ':' || of[end - 1] != ':'); end -= 1)
      depth = of[end - 1] == '>' ? depth + 1 : of[end - 1] == '<' && depth > 0 ? depth - 1 : depth;
    o->scratch.n = 0;
    if (end >= 2) memcpy(add(&o->scratch, end), of, end);
    if (name.n) memcpy(add(&o->scratch, name.n), name.p, name.n);
    if (o->scratch.n == 0) return;
    RdiSymbol v = {.name = intern(&o->strings, o->scratch.p, (uint32_t)o->scratch.n), .location = 8ull << 56 | o->s[RDI_LOCATION_CONSTANTS].n, .type = type};
    named(&o->pairs, RDI_NAMES_CONSTANTS, v.name, COUNT(o->s[RDI_CONSTANTS], RdiSymbol));
    *(uint64_t *)add(&o->s[RDI_LOCATION_CONSTANTS], 8) = value;
    *(RdiSymbol *)add(&o->s[RDI_CONSTANTS], sizeof v) = v;
  } else {                                                 /* CV_SymUDT: type, name: an alias */
    PdbSpan name = pdb_string(d, &(uint32_t){4});
    type_node(o, (RdiType){.kind = 0x2004, .byte_size = y->byte_size, .a = intern(&o->strings, name.p, name.n), .b = type});
  }
}

/* What is known of the procedure being read: its S_FRAMEPROC (size, flags), whether its
   locals are S_LOCAL records (CV_ProcFlag_OptDbgInfo), how many S_REGREL32 it has had, and the
   local that S_DEFRANGE records add places to (1 + its index; 0: none). */
typedef struct { uint32_t frame_size, frame_flags, regrels, target; bool framed, optimised; } Frame;

/* RDI_RegCodeX64 for a CV_Reg: the general registers at every width, and xmm0 to xmm15 whole,
   as four floats and as two doubles (a float or double local of optimised code is in one of
   those parts). 0: the rest of CV_Reg_X64_XList, which no local of the files measured uses. */
static uint64_t reg_code(uint32_t cv)
{
  static const uint8_t wide[] = {1, 4, 2, 3, 7, 8, 6, 5}, low[] = {7, 8, 6, 5};   /* rax rbx rcx rdx rsi rdi rbp rsp; sil dil bpl spl */
  return cv == 0 ? 0 : cv <= 8 ? (cv - 1) % 4 + 1 : cv <= 24 ? (cv - 9) % 8 + 1 : cv >= 154 && cv <= 161 ? cv - 113
       : cv >= 162 && cv <= 193 ? (cv - 162) / 4 + 41 : cv >= 194 && cv <= 209 ? (cv - 194) % 8 + 41 : cv >= 252 && cv <= 259 ? cv - 203
       : cv >= 260 && cv <= 291 ? (cv - 260) / 4 + 49 : cv >= 292 && cv <= 307 ? (cv - 292) % 8 + 49 : cv >= 324 && cv <= 327 ? low[cv - 324] : cv >= 328 && cv <= 335 ? wide[cv - 328]
       : cv >= 336 && cv <= 367 ? (cv - 336) % 8 + 9 : 0;
}

/* The location "at register + offset" (cv2r_location_from_addr_reg_off), or with `indirect`
   "at the address stored there": the register and a 16-bit offset in the location itself; a
   wider offset as bytecode (read the 8-byte register, push the offset as 32 bits and widen it
   with its sign, add, [read 8 bytes], end) that the location names by its offset. */
static uint64_t reg_plus(Out *o, uint64_t reg, int32_t off, bool indirect)
{
  if (off >= INT16_MIN && off <= INT16_MAX) return (indirect ? 4ull : 3ull) << 56 | reg << 48 | (uint64_t)(uint16_t)off << 32;
  uint64_t at = o->s[RDI_LOCATION_BYTECODE].n;
  uint8_t code[] = {5, (uint8_t)reg, 8, 0, 0, 13, (uint8_t)off, (uint8_t)(off >> 8), (uint8_t)(off >> 16), (uint8_t)(off >> 24), 42, 32, 21, 0, 0, 4, 8, 0};
  if (!indirect) code[15] = 0;
  memcpy(add(&o->s[RDI_LOCATION_BYTECODE], indirect ? 18 : 16), code, indirect ? 18 : 16);
  return 1ull << 56 | at;
}

/* The places of the target local over a CV_LvarAddrRange (offset, section, length at `at`)
   minus the gaps listed after it (cv2r_location_case_list_push_over_lvar_addr_range). */
static void ranged(Out *o, const Pdb *pdb, PdbSpan d, uint32_t at, uint64_t location)
{
  uint64_t first = pdb_voff(pdb, le16(d.p + at + 4), le32(d.p + at)), cursor = first;
  for (uint32_t gap = at + 8; d.n - gap >= 4; gap += 4) {   /* CV_LvarAddrGap: offset from the range's start, length */
    if (cursor < first + le16(d.p + gap)) rec(&o->cases, cursor, first + le16(d.p + gap), location, 0);
    cursor = first + le16(d.p + gap) + le16(d.p + gap + 2);
  }
  if (cursor < first + le16(d.p + at + 6)) rec(&o->cases, cursor, first + le16(d.p + at + 6), location, 0);
}

/* A local variable's record in the scope `top` ("symbols pass 2"). Locals are kept in record
   order with their scope in `container` and, for those with S_DEFRANGE records, their first
   case in `root_scope` and how many in `link_name`, until `locals_by_scope` puts each scope's together.
   S_REGREL32 (unoptimised MSVC): at register + offset, everywhere. It is a parameter when it
   is among the first as many as the procedure's type has; a parameter that does not fit a
   register (over 8 bytes, or not 1, 2, 4 or 8) is at the address stored there. One without a
   type beyond the frame is where the caller wants a large return value: it gets the return type.
   S_LOCAL (optimised MSVC, clang): a local whose places are the S_DEFRANGE records after it. */
static void local_symbol(Out *o, const Pdb *pdb, Frame *f, uint32_t top, uint16_t kind, PdbSpan d)
{
  const RdiSymbol *proc = (const RdiSymbol *)o->s[RDI_PROCEDURES].p + ((const RdiScope *)o->s[RDI_SCOPES].p)[top].proc;
  uint64_t before = o->cases.n;
  RdiSymbol *target = (RdiSymbol *)o->s[RDI_LOCALS].p + f->target;
  if (kind == S_REGREL32 && d.n >= 10) {                   /* CV_SymRegrel32: offset, type, register, name */
    uint32_t nth = f->regrels++, off = le32(d.p), type = type_of(o, le32(d.p + 4)), reg = le16(d.p + 8);
    if (f->optimised || !top) return;
    const RdiType *types = (const RdiType *)o->s[RDI_TYPE_NODES].p, *made = types + proc->type;
    bool param = (made->kind == 0x1005 || made->kind == 0x1006) && nth < made->b, big = types[type].byte_size > 8 || (types[type].byte_size & (types[type].byte_size - 1)) != 0;
    bool returned = (reg == 334 || reg == 335) && f->framed && off > f->frame_size && le32(d.p + 4) == 0 && proc->type != 0;   /* rbp, rsp */
    PdbSpan name = pdb_string(d, &(uint32_t){10});
    *(RdiSymbol *)add(&o->s[RDI_LOCALS], sizeof(RdiSymbol)) = (RdiSymbol){.name = intern(&o->strings, name.p, name.n), .flags = param, .type = returned ? made->a : type, .container = top, .location = reg_plus(o, reg_code(reg), (int32_t)off, returned || (param && big))};
    return;
  } else if (kind == S_LOCAL && d.n >= 6 && top) {         /* CV_SymLocal: type, flags (1 parameter, 0x200 global, 0x400 static), name */
    PdbSpan name = pdb_string(d, &(uint32_t){6});
    f->target = le16(d.p + 4) & 0x600 ? 0 : COUNT(o->s[RDI_LOCALS], RdiSymbol);
    if (f->target) *(RdiSymbol *)add(&o->s[RDI_LOCALS], sizeof(RdiSymbol)) = (RdiSymbol){.name = intern(&o->strings, name.p, name.n), .flags = le16(d.p + 4) & 1, .type = type_of(o, le32(d.p)), .container = top, .root_scope = (uint32_t)(before / 32)};
  } else if (kind == S_FILESTATIC) f->target = 0;
  else if (!f->target) return;
  else if (kind == S_DEFRANGE_REGISTER && d.n >= 12) ranged(o, pdb, d, 4, 5ull << 56 | reg_code(le16(d.p)) << 48);   /* register, attributes, range: the value is in the register */
  else if (kind == S_DEFRANGE_SUBFIELD_REGISTER && d.n >= 16 && le32(d.p + 4) == 0) ranged(o, pdb, d, 8, 5ull << 56 | reg_code(le16(d.p)) << 48);   /* only a part at offset 0 */
  else if (kind == S_DEFRANGE_REGISTER_REL && d.n >= 16) ranged(o, pdb, d, 8, reg_plus(o, reg_code(le16(d.p)), (int32_t)le32(d.p + 4), false));   /* register, flags, offset, range */
  else if ((kind == S_DEFRANGE_FRAMEPOINTER_REL && d.n >= 12) || (kind == S_DEFRANGE_FRAMEPOINTER_REL_FULL_SCOPE && d.n >= 4)) {
    /* offset from the frame pointer, which S_FRAMEPROC's flags name for parameters and for the rest: 1 rsp, 2 rbp, 3 r13 */
    static const uint8_t pointer[] = {0, 5, 6, 14};
    if (!f->framed) return;
    uint64_t location = reg_plus(o, pointer[f->frame_flags >> (target->flags & 1 ? 16 : 14) & 3], (int32_t)le32(d.p), false);
    if (kind == S_DEFRANGE_FRAMEPOINTER_REL) ranged(o, pdb, d, 4, location);
    else rec(&o->cases, 0, UINT64_MAX, location, 0);
  }
  if (f->target) ((RdiSymbol *)o->s[RDI_LOCALS].p)[f->target].link_name += (uint32_t)((o->cases.n - before) / 32);
}

/* Puts each scope's locals together, in record order, and writes each local's location: none
   without a case; the case itself when it holds everywhere; else a set (kind 9: the count
   above bit 32, the first element below) of (first, end, location).
   RAD writes a lone case as the location itself whatever it covers, so a parameter that is in
   rcx for the first 4 bytes of an optimised function reads rcx all the way down; here a case
   that does not hold everywhere is always in a set. */
static void locals_by_scope(Out *o)
{
  uint32_t n = COUNT(o->s[RDI_LOCALS], RdiSymbol), scopes = COUNT(o->s[RDI_SCOPES], RdiScope);
  RdiScope *s = (RdiScope *)o->s[RDI_SCOPES].p;
  RdiSymbol *all = (RdiSymbol *)o->s[RDI_LOCALS].p, *in = malloc((n + 1) * sizeof *in);
  const uint64_t *c = (const uint64_t *)o->cases.p;
  if (!in) abort();
  memcpy(in, all, n * sizeof *in);
  for (uint32_t i = 1; i < n; i += 1) s[in[i].container].local_count += 1;
  for (uint32_t i = 1, first = 1; i < scopes; first += s[i].local_count, i += 1) s[i].local_first = first;
  for (uint32_t i = 1; i < scopes; i += 1) s[i].local_count = 0;
  for (uint32_t i = 1; i < n; i += 1) {
    RdiSymbol *y = all + s[in[i].container].local_first + s[in[i].container].local_count++;
    uint64_t mine = 4 * (uint64_t)in[i].root_scope;   /* its first case, when it has any */
    *y = (RdiSymbol){.name = in[i].name, .flags = in[i].flags, .container_flags = 2, .container = in[i].container, .type = in[i].type, .location = in[i].location};   /* RDI_ContainerKind_Scope */
    if (in[i].link_name == 1 && c[mine] == 0 && c[mine + 1] == UINT64_MAX) y->location = c[mine + 2];
    else if (in[i].link_name != 0) y->location = 9ull << 56 | (uint64_t)in[i].link_name << 32 | COUNT(o->s[RDI_LOCATION_SETS], RdiLocationCase);
    for (uint32_t k = 0; y->location >> 56 == 9 && k < in[i].link_name; k += 1)
      *(RdiLocationCase *)add(&o->s[RDI_LOCATION_SETS], sizeof(RdiLocationCase)) = (RdiLocationCase){c[mine + 4 * k], c[mine + 4 * k + 1], c[mine + 4 * k + 2]};
  }
  free(in);
}

/* A line table from records (offset, order, file, line), sorted here. */
static uint32_t line_table(Out *o, Buf *lines)
{
  uint64_t n, *l = sorted(lines, &n);
  RdiLineTable t = {.voffs_base = COUNT(o->s[RDI_LINE_VOFFS], uint64_t), .lines_base = COUNT(o->s[RDI_LINE_LINES], RdiLine), .lines_count = (uint32_t)n};
  for (uint64_t i = 0; i < n; i += 1) {
    *(uint64_t *)add(&o->s[RDI_LINE_VOFFS], 8) = l[4 * i];
    *(RdiLine *)add(&o->s[RDI_LINE_LINES], sizeof(RdiLine)) = (RdiLine){(uint32_t)l[4 * i + 2], (uint32_t)l[4 * i + 3]};
  }
  *(uint64_t *)add(&o->s[RDI_LINE_VOFFS], 8) = UINT64_MAX;   /* RAD's reader looks one past the last */
  *(RdiLineTable *)add(&o->s[RDI_LINE_TABLES], sizeof t) = t;
  return COUNT(o->s[RDI_LINE_TABLES], RdiLineTable) - 1;
}

/* A procedure and its root scope. */
static uint32_t procedure(Out *o, RdiSymbol y, uint64_t first, uint64_t opl)
{
  uint32_t idx = COUNT(o->s[RDI_PROCEDURES], RdiSymbol);
  pair(&o->s[RDI_SCOPE_VOFFS], first, opl);
  y.root_scope = scope(o, idx, 0, 0, o->s[RDI_SCOPE_VOFFS].n - 16);
  *(RdiSymbol *)add(&o->s[RDI_PROCEDURES], sizeof y) = y;
  named(&o->pairs, RDI_NAMES_PROCEDURES, y.name, idx);
  named(&o->pairs, RDI_NAMES_LINK_PROCEDURES, y.link_name, idx);
  return idx;
}

/* Each record's children as a list in order of creation. A record is uint32s with its
   parent, first child and next sibling at 1, 2 and 3; records without a parent are not listed. */
static void link_children(Buf *table, uint64_t size)
{
  for (uint64_t k = table->n / size - 1; k > 0; k -= 1) {
    uint32_t *r = (uint32_t *)(table->p + k * size), *parent = (uint32_t *)(table->p + r[1] * size);
    if (r[1] != 0) r[3] = parent[2], parent[2] = (uint32_t)k;
  }
}

/* A vmap from ranges (first, ~end, index): at each offset the answer is the covering range
   that sorts last, so a range inside another wins. RAD's version of this loop
   (rdi_make_local.c, "bake vmaps") only drops the innermost range when it ends; when ranges
   overlap without nesting, one that ended earlier stays underneath and is written out later,
   out of order. Here everything that has ended is dropped at once, so keys only ascend. */
static void vmap_build(Buf *out, Buf *ranges)
{
  uint64_t n, *r = sorted(ranges, &n), top = 0;
  uint64_t *stack = malloc((n + 1) * sizeof *stack);
  if (!stack) abort();
  for (uint64_t i = 0; i <= n; i += 1) {
    uint64_t start = i < n ? r[4 * i] : UINT64_MAX;
    for (int ended = 1; ended;) {
      ended = top > 0 && ~r[4 * stack[top - 1] + 1] <= start;
      uint64_t end = ended ? ~r[4 * stack[top - 1] + 1] : start;
      while (top > 0 && ~r[4 * stack[top - 1] + 1] <= end) top -= 1;
      if (!ended && i == n) break;
      if (!ended) stack[top++] = i;
      /* one entry per offset: a later answer at the same offset replaces the earlier */
      if (!out->n || LAST(*out, RdiVMap)->voff != end) add(out, sizeof(RdiVMap));
      *LAST(*out, RdiVMap) = (RdiVMap){end, top ? r[4 * stack[top - 1] + 2] : 0};
    }
  }
  free(stack);
}

/* Name maps: for each kind, one node per distinct string holding the indices filed under it,
   in buckets by rdi_hash of the string. */
static void name_maps(Out *o)
{
  uint64_t n, *p = sorted(&o->pairs, &n), strings[RDI_NAMES_KINDS] = {0};
  uint32_t *at = (uint32_t *)o->strings.at.p;
  for (uint64_t i = 0; i < n; i += 1) if (i == 0 || p[4 * i] != p[4 * i - 4] || p[4 * i + 1] != p[4 * i - 3]) strings[p[4 * i]] += 1;
  for (uint64_t i = 0; i < n; i += 1) {   /* now that the bucket counts are known: sort by kind, bucket, string, index */
    uint64_t s = p[4 * i + 1];
    p[4 * i + 3] = p[4 * i + 2], p[4 * i + 2] = s;
    p[4 * i + 1] = rdi_hash(o->strings.keys.p + at[s], at[s + 1] - at[s]) % strings[p[4 * i]];
  }
  p = sorted(&o->pairs, &n);
  RdiNameMap *maps = add(&o->s[RDI_NAME_MAPS], RDI_NAMES_KINDS * sizeof *maps);
  for (uint64_t i = 0, kind = 0; kind < RDI_NAMES_KINDS; kind += 1) {
    maps[kind] = (RdiNameMap){COUNT(o->s[RDI_NAME_MAP_BUCKETS], RdiBucket), COUNT(o->s[RDI_NAME_MAP_NODES], RdiNameNode), (uint32_t)strings[kind], (uint32_t)strings[kind]};
    RdiBucket *buckets = add(&o->s[RDI_NAME_MAP_BUCKETS], strings[kind] * sizeof *buckets);
    for (uint32_t node = 0; i < n && p[4 * i] == kind; node += 1) {
      uint64_t first = i, bucket = p[4 * i + 1];
      while (i < n && p[4 * i] == kind && p[4 * i + 2] == p[4 * first + 2]) i += 1;
      if (buckets[bucket].node_count++ == 0) buckets[bucket].first_node = node;
      /* one index sits in the node, more are a run of indices */
      uint32_t count = (uint32_t)(i - first), match = count == 1 ? (uint32_t)p[4 * first + 3] : COUNT(o->s[RDI_INDEX_RUNS], uint32_t);
      for (uint64_t j = first; count > 1 && j < i; j += 1) *(uint32_t *)add(&o->s[RDI_INDEX_RUNS], 4) = (uint32_t)p[4 * j + 3];
      *(RdiNameNode *)add(&o->s[RDI_NAME_MAP_NODES], sizeof(RdiNameNode)) = (RdiNameNode){(uint32_t)p[4 * first + 2], count, match};
    }
  }
}

/* The path of a node as RAD normalises it: parts joined by '/', ASCII lower case
   (rdim_normalized_from_path). Its string index. */
static uint32_t normal_path(Out *o, uint32_t node, Buf *scratch)
{
  uint32_t depth = 0, *at = (uint32_t *)o->strings.at.p;
  const RdiPathNode *nodes = (const RdiPathNode *)o->s[RDI_FILE_PATH_NODES].p;
  for (uint32_t k = node; k != 0; k = nodes[k].parent) depth += 1;
  scratch->n = 0;
  for (uint32_t d = depth; d > 0; d -= 1) {
    uint32_t k = node;
    for (uint32_t up = 1; up < d; up += 1) k = nodes[k].parent;
    uint32_t len = at[nodes[k].name + 1] - at[nodes[k].name];
    uint8_t *to = add(scratch, len + (d > 1));
    for (uint32_t i = 0; i < len; i += 1) {
      uint8_t c = o->strings.keys.p[at[nodes[k].name] + i];
      to[i] = c >= 'A' && c <= 'Z' ? c + 32 : c;
    }
    if (d > 1) to[len] = '/';
  }
  return intern(&o->strings, scratch->p, (uint32_t)scratch->n);
}

uint8_t *rdi_from_pdb(const void *data, uint64_t size, uint64_t *out_size)
{
  Pdb pdb;
  if (!pdb_open(&pdb, data, size)) return 0;
  Out o = {0};
  Buf lines = {0}, maps = {0}, ranges = {0}, open = {0}, links = {0}, id_ats = {0};
  /* index 0 of every table is the record that means "none" */
  static const int tables[] = {RDI_BINARY_SECTIONS, RDI_FILE_PATH_NODES, RDI_SOURCE_FILES, RDI_LINE_TABLES, RDI_SOURCE_LINE_MAPS, RDI_UNITS, RDI_SCOPES, RDI_PROCEDURES, RDI_INLINE_SITES, RDI_TYPE_NODES, RDI_INDEX_RUNS, RDI_UDTS, RDI_MEMBERS, RDI_ENUM_MEMBERS, RDI_GLOBALS, RDI_THREAD_VARS, RDI_CONSTANTS, RDI_LOCALS, RDI_LOCATION_SETS};
  static const uint8_t table_size[] = {sizeof(RdiSection), sizeof(RdiPathNode), sizeof(RdiSourceFile), sizeof(RdiLineTable), sizeof(RdiSourceLineMap), sizeof(RdiUnit), sizeof(RdiScope), sizeof(RdiSymbol), sizeof(RdiInlineSite), sizeof(RdiType), 4, sizeof(RdiUdt), sizeof(RdiMember), sizeof(RdiEnumMember), sizeof(RdiSymbol), sizeof(RdiSymbol), sizeof(RdiSymbol), sizeof(RdiSymbol), sizeof(RdiLocationCase)};
  for (unsigned i = 0; i < sizeof tables / sizeof *tables; i += 1) add(&o.s[tables[i]], table_size[i]);
  intern(&o.strings, "", 0);
  intern(&o.nodes, (uint32_t[2]){0, 0}, 8);
  intern(&o.files, (uint32_t[1]){0}, 4);

  *(uint32_t *)add(&o.s[RDI_TOP_LEVEL_INFO], 48) = pdb.machine == 0x8664 ? 1 : pdb.machine == 0x14c ? 2 : 0;   /* RDI_Arch */

  for (uint32_t i = 1; i <= pdb.sections; i += 1) {
    PdbSection s = pdb_section(&pdb, i);
    RdiSection *b = add(&o.s[RDI_BINARY_SECTIONS], sizeof *b);
    b->name = intern(&o.strings, s.name.p, s.name.n);
    b->flags = (s.flags >> 30 & 1) | (s.flags >> 31 & 1) << 1 | (s.flags >> 29 & 1) << 2;   /* read, write, execute */
    b->voff = s.voff, b->voff_opl = (uint64_t)s.voff + s.vsize, b->foff = s.foff, b->foff_opl = (uint64_t)s.foff + s.fsize;
  }

  PdbModule m;
  PdbSpan syms, c13, d, ids;
  uint32_t first_id, first_type;
  uint8_t *id_stream = pdb_leaves(&pdb, PDB_IDS, &ids, &first_id);
  for (uint32_t at = 0, next = 0; pdb_symbol(ids, &next, &(uint16_t){0}, &(PdbSpan){0}); at = next) *(uint32_t *)add(&id_ats, 4) = at;
  uint16_t kind;
  uint64_t order = 0;

  /* Types: every record's type is made, as RAD makes them, after the full definitions are filed
     by name. Type ids start at 0x1000 in every PDB; one that says otherwise has no types here. */
  uint8_t *type_stream = pdb_leaves(&pdb, PDB_TYPES, &o.leaves, &first_type);
  o.pointer_size = pdb.machine == 0x8664 ? 8 : 4;
  if (first_type != FIRST_RECORD) o.leaves.n = 0;
  for (uint32_t at = 0, next = 0; pdb_symbol(o.leaves, &next, &kind, &d); at = next) {
    uint16_t props;
    uint64_t size;
    PdbSpan name;
    *(uint32_t *)add(&o.leaf_at, 4) = at;
    if (!named_leaf(kind, d, &props, &size, &name) || props >> 7 & 1) continue;
    defined(&o, name, FIRST_RECORD + COUNT(o.leaf_at, uint32_t) - 1);
  }
  add(&o.made, 4 * (FIRST_RECORD + (uint64_t)COUNT(o.leaf_at, uint32_t)));
  for (uint32_t id = FIRST_RECORD; id < COUNT(o.made, uint32_t); id += 1)
    if (!type_of(&o, id)) record_type(&o, id);
  for (uint32_t id = FIRST_RECORD; id < COUNT(o.made, uint32_t); id += 1) members(&o, id);
  for (uint32_t at = 0; pdb_module(&pdb, &at, &m);) {
    /* a unit is named by the last part of its object file's path (rdi_from_codeview.c) */
    PdbSpan name = m.name;
    for (uint32_t i = 0; i + 1 < m.name.n; i += 1) if (slash(m.name.p[i])) name = (PdbSpan){m.name.p + i + 1, m.name.n - i - 1};
    RdiUnit unit = {.name = intern(&o.strings, name.p, name.n), .object_path = path_node(&o, 0, m.name), .archive_path = path_node(&o, 0, m.obj)};
    named(&o.pairs, RDI_NAMES_UNITS, unit.name, COUNT(o.s[RDI_UNITS], RdiUnit));

    /* Lines: every record, and after each block a record that ends it. Sorted by offset, an
       end sorts before a line at the same offset and later lines after earlier ones, because
       our reader takes the last record at an offset. RAD sorts by offset only, and its reader
       can then answer "no line" at the first byte of a function that follows another without a gap. */
    uint8_t *stream = pdb_module_stream(&pdb, &m, &syms, &c13);
    PdbLineBlock b;
    PdbLineCursor cur = {0};
    uint32_t folder = ((RdiPathNode *)o.s[RDI_FILE_PATH_NODES].p)[unit.object_path].parent;
    lines.n = 0;
    while (pdb_line_block(&pdb, c13, &cur, &b)) {
      uint32_t file = source_file(&o, &b, folder);
      for (uint32_t j = 0; j < b.count; j += 1) {
        uint64_t voff = b.base + le32(b.lines + 8 * j), line = le32(b.lines + 8 * j + 4) & 0xffffff;
        rec(&lines, voff, 1ull << 63 | order++, file, line);
        rec(&maps, file, line, voff, 0);
      }
      rec(&lines, b.opl, order++, 0, 0);
    }
    unit.line_table = line_table(&o, &lines);

    /* Procedures and scopes (rdi_from_codeview.c, "symbols pass 2"): a procedure opens its root
       scope; a block or an inlined call opens a scope inside the open one; S_END and
       S_INLINESITE_END close the open one. A thunk is a procedure that opens nothing. `open` is
       the stack of open scopes; every module leaves it empty. */
    unit.procs_first = COUNT(o.s[RDI_PROCEDURES], RdiSymbol);
    uint64_t base = 0;                                                      /* where the procedure being read starts */
    Frame frame = {0};
    for (uint32_t at = 0; pdb_symbol(syms, &at, &kind, &d);) {
      uint32_t top = open.n ? *LAST(open, uint32_t) : 0, made = 0, inside = ((RdiScope *)o.s[RDI_SCOPES].p)[top].proc;
      if (kind == S_END || kind == S_INLINESITE_END) open.n -= open.n ? 4 : 0;
      else if ((kind == S_GPROC32 || kind == S_LPROC32) && d.n >= 35) {   /* CV_SymProc32 */
        uint64_t voff = base = pdb_voff(&pdb, le16(d.p + 32), le32(d.p + 28));
        PdbSpan name = pdb_string(d, &(uint32_t){35});
        uint32_t proc = procedure(&o, (RdiSymbol){.name = intern(&o.strings, name.p, name.n), .container_flags = kind == S_GPROC32 ? 0x80 : 0, .type = type_of(&o, le32(d.p + 24))}, voff, voff + le32(d.p + 12));
        rec(&links, voff, 0, proc, 0);
        made = COUNT(o.s[RDI_SCOPES], RdiScope) - 1;
        /* its frame is the last S_FRAMEPROC before the next procedure, wherever the compiler put it ("symbols pass 1") */
        frame = (Frame){.optimised = d.p[34] & 0x80};
        PdbSpan r;
        for (uint32_t next = at; pdb_symbol(syms, &next, &kind, &r) && !((kind == S_GPROC32 || kind == S_LPROC32) && r.n >= 35);)
          if (kind == S_FRAMEPROC && r.n >= 26) frame.frame_size = le32(r.p), frame.frame_flags = le32(r.p + 22), frame.framed = true;   /* CV_SymFrameproc */
      } else if (kind == S_THUNK32 && d.n >= 21) {                         /* CV_SymThunk32 */
        uint64_t voff = pdb_voff(&pdb, le16(d.p + 16), le32(d.p + 12));
        PdbSpan name = pdb_string(d, &(uint32_t){21});
        procedure(&o, (RdiSymbol){.name = intern(&o.strings, name.p, name.n), .flags = 2}, voff, voff + le16(d.p + 18));
      } else if (kind == S_BLOCK32 && d.n >= 18) {                         /* CV_SymBlock32 */
        uint64_t voff = pdb_voff(&pdb, le16(d.p + 16), le32(d.p + 12));
        pair(&o.s[RDI_SCOPE_VOFFS], voff, voff + le32(d.p + 8));
        made = scope(&o, inside, top, 0, o.s[RDI_SCOPE_VOFFS].n - 16);
      } else if (kind == S_INLINESITE && d.n >= 12) {                       /* CV_SymInlineSite: parent, end, id, annotations */
        uint64_t start = o.s[RDI_SCOPE_VOFFS].n;
        uint32_t id = le32(d.p + 8), sum_at = 0, line = 0, file = 0;
        bool listed = pdb_inlinee(c13, id, &sum_at, &line);
        lines.n = 0;
        inline_pieces(&o.s[RDI_SCOPE_VOFFS], &lines, (PdbSpan){d.p + 12, d.n - 12}, base, sum_at, line);
        if (!listed) lines.n = 0;                                           /* no first line to count from: no lines */
        uint64_t *l = (uint64_t *)lines.p;
        for (uint64_t i = 0, last = UINT64_MAX; i < lines.n / 32; i += 1) {   /* each line's file, from the offset of its checksum */
          if (l[4 * i + 1] >> 63 == 0) continue;
          if (l[4 * i + 2] != last) {
            last = l[4 * i + 2];
            PdbLineBlock f = pdb_file(&pdb, cur.sums, (uint32_t)last);
            file = source_file(&o, &f, folder);
          }
          l[4 * i + 2] = file;
          rec(&maps, file, l[4 * i + 3], l[4 * i], 0);
        }
        /* its name is in the id record it names: two type ids, then the name (CV_LeafFuncId, CV_LeafMFuncId) */
        id -= first_id;
        uint32_t id_at = id < COUNT(id_ats, uint32_t) ? ((uint32_t *)id_ats.p)[id] : ids.n;
        PdbSpan name = pdb_symbol(ids, &id_at, &kind, &d) && (kind == LF_FUNC_ID || kind == LF_MFUNC_ID) ? pdb_string(d, &(uint32_t){8}) : (PdbSpan){0};
        made = scope(&o, inside, top, COUNT(o.s[RDI_INLINE_SITES], RdiInlineSite), start);
        *(RdiInlineSite *)add(&o.s[RDI_INLINE_SITES], sizeof(RdiInlineSite)) = (RdiInlineSite){.name = intern(&o.strings, name.p, name.n), .line_table = line_table(&o, &lines)};
      } else data_symbol(&o, &pdb, kind, d, false), local_symbol(&o, &pdb, &frame, top, kind, d);
      if (made) *(uint32_t *)add(&open, 4) = made;
    }
    unit.procs_count = COUNT(o.s[RDI_PROCEDURES], RdiSymbol) - unit.procs_first;
    free(stream);
    *(RdiUnit *)add(&o.s[RDI_UNITS], sizeof unit) = unit;
  }

  /* which unit each piece of the image came from (pdb_comp_unit_contribution_array_from_data) */
  for (uint32_t i = 0; i < pdb_contribs(&pdb); i += 1) {
    PdbContrib c = pdb_contrib(&pdb, i);
    uint64_t voff = pdb_voff(&pdb, c.sec, c.off);
    if (voff != 0 && c.size != 0) rec(&ranges, voff, ~(voff + c.size), (uint64_t)c.mod + 1, 0);
  }
  vmap_build(&o.s[RDI_UNIT_VMAP], &ranges);

  /* Link names (rdi_from_codeview.c, "build link name map" and "produce stub procedures from all
     pub32 symbols"). At one offset, sorted: the procedures that start there, then the public
     symbols in file order. The procedures take the name of the last public symbol. Where no
     procedure starts, each public symbol of code becomes a procedure that runs to the next
     boundary of the unit map, which is the end of its contribution. (RAD gives a symbol outside
     every contribution one byte; no file we have holds one, and here it runs to the next
     contribution, or gets no procedure after the last.) */
  uint8_t *globals = pdb_globals(&pdb, &syms);
  for (uint32_t at = 0; pdb_symbol(syms, &at, &kind, &d);) {
    uint64_t voff = kind == S_PUB32 && d.n >= 10 ? pdb_voff(&pdb, le16(d.p + 8), le32(d.p + 4)) : 0;   /* CV_SymPub32 */
    data_symbol(&o, &pdb, kind, d, true);
    if (voff == 0) continue;
    PdbSpan name = pdb_string(d, &(uint32_t){10});
    rec(&links, voff, ++order, intern(&o.strings, name.p, name.n), le32(d.p) & 3);   /* code or function */
  }
  free(globals);
  free(id_stream);
  free(type_stream);
  uint64_t n, *l = sorted(&links, &n);
  const RdiVMap *held = (const RdiVMap *)o.s[RDI_UNIT_VMAP].p;   /* null when there are no contributions: indexed, never offset */
  uint32_t next = 0, none = COUNT(o.s[RDI_UNIT_VMAP], RdiVMap);
  for (uint64_t i = 0, first = 0; i < n; first = i) {
    while (i < n && l[4 * i] == l[4 * first]) i += 1;
    uint64_t voff = l[4 * first], last = i - 1;
    if (l[4 * last + 1] == 0) continue;                    /* no public symbol here */
    while (next < none && held[next].voff <= voff) next += 1;
    for (uint64_t j = first; j < i; j += 1) {
      if (l[4 * j + 1] == 0) {
        RdiSymbol *y = (RdiSymbol *)o.s[RDI_PROCEDURES].p + l[4 * j + 2];
        y->link_name = (uint32_t)l[4 * last + 2];
        named(&o.pairs, RDI_NAMES_LINK_PROCEDURES, y->link_name, l[4 * j + 2]);
      } else if (l[4 * first + 1] != 0 && l[4 * j + 3] != 0 && next < none)
        procedure(&o, (RdiSymbol){.name = (uint32_t)l[4 * j + 2], .link_name = (uint32_t)l[4 * j + 2]}, voff, held[next].voff);
    }
  }
  vmap_build(&o.s[RDI_SCOPE_VMAP], &o.scopes);
  vmap_build(&o.s[RDI_GLOBAL_VMAP], &o.globals);
  locals_by_scope(&o);
  for (uint32_t i = 1; i < COUNT(o.s[RDI_TYPE_NODES], RdiType); i += 1) {   /* every type with a name is found by it */
    const RdiType *y = (const RdiType *)o.s[RDI_TYPE_NODES].p + i;
    if (y->kind < 0x1000 || (y->kind >= 0x2000 && y->kind < 0xf000)) named(&o.pairs, RDI_NAMES_TYPES, y->a, i);
  }
  link_children(&o.s[RDI_SCOPES], sizeof(RdiScope));

  /* per source file: its lines that have code, ascending, each with its offsets */
  uint64_t *r = sorted(&maps, &n);
  for (uint32_t file = 1; file < COUNT(o.s[RDI_SOURCE_FILES], RdiSourceFile); file += 1) add(&o.s[RDI_SOURCE_LINE_MAPS], sizeof(RdiSourceLineMap));
  for (uint64_t i = 0; i < n; i += 1) {
    RdiSourceLineMap *map = (RdiSourceLineMap *)o.s[RDI_SOURCE_LINE_MAPS].p + r[4 * i];
    if (i == 0 || r[4 * i] != r[4 * i - 4]) {
      map->nums_base = COUNT(o.s[RDI_SOURCE_LINE_NUMS], uint32_t), map->voffs_base = COUNT(o.s[RDI_SOURCE_LINE_VOFFS], uint64_t);
      map->ranges_base = COUNT(o.s[RDI_SOURCE_LINE_RANGES], uint32_t);
      add(&o.s[RDI_SOURCE_LINE_RANGES], 4);              /* line_count + 1 bounds; the first is 0 */
    }
    if (map->line_count == 0 || r[4 * i + 1] != r[4 * i - 3]) {
      *(uint32_t *)add(&o.s[RDI_SOURCE_LINE_NUMS], 4) = (uint32_t)r[4 * i + 1];
      add(&o.s[RDI_SOURCE_LINE_RANGES], 4);
      map->line_count += 1;
    }
    *(uint64_t *)add(&o.s[RDI_SOURCE_LINE_VOFFS], 8) = r[4 * i + 2];
    *LAST(o.s[RDI_SOURCE_LINE_RANGES], uint32_t) = ++map->voff_count;
  }

  link_children(&o.s[RDI_FILE_PATH_NODES], sizeof(RdiPathNode));
  /* each source file's normal path */
  for (uint32_t file = 1; file < COUNT(o.s[RDI_SOURCE_FILES], RdiSourceFile); file += 1) {
    uint32_t path = normal_path(&o, ((RdiSourceFile *)o.s[RDI_SOURCE_FILES].p)[file].path_node, &o.scratch);
    ((RdiSourceFile *)o.s[RDI_SOURCE_FILES].p)[file].path = path;
    named(&o.pairs, RDI_NAMES_SOURCE_PATHS, path, file);
  }
  name_maps(&o);
  o.s[RDI_STRING_DATA] = o.strings.keys, o.s[RDI_STRING_TABLE] = o.strings.at;

  /* the file: header, the table of sections, then each section at a multiple of 8 */
  Buf file = {0};
  struct { uint64_t magic; uint32_t version, sections_off, sections_count; } *h = add(&file, 24);   /* RDI_Header */
  h->magic = 0x0000676264646172u, h->version = 23, h->sections_off = 24, h->sections_count = RDI_SECTIONS;
  add(&file, RDI_SECTIONS * 32);
  for (int k = 0; k < RDI_SECTIONS; k += 1) {
    uint64_t *sec = (uint64_t *)(file.p + 24 + 32 * k);    /* encoding 0; offset; size; unpacked size */
    sec[1] = file.n, sec[2] = sec[3] = o.s[k].n;
    if (o.s[k].n) memcpy(add(&file, (o.s[k].n + 7) / 8 * 8), o.s[k].p, o.s[k].n);
    free(o.s[k].p);
  }
  Buf *rest[] = {&o.strings.slots, &o.nodes.keys, &o.nodes.at, &o.nodes.slots, &o.files.keys, &o.files.at, &o.files.slots, &o.pairs, &o.scopes, &o.globals, &o.cases, &lines, &maps, &ranges, &open, &links, &id_ats,
                 &o.leaf_at, &o.made, &o.scratch, &o.defined_id, &o.defined.keys, &o.defined.at, &o.defined.slots};
  for (unsigned i = 0; i < sizeof rest / sizeof *rest; i += 1) free(rest[i]->p);
  pdb_close(&pdb);
  *out_size = file.n;
  return file.p;
}
