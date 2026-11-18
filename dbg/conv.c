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

enum { S_END = 0x0006, S_THUNK32 = 0x1102, S_BLOCK32 = 0x1103, S_PUB32 = 0x110e, S_LPROC32 = 0x110f, S_GPROC32 = 0x1110,
       S_INLINESITE = 0x114d, S_INLINESITE_END = 0x114e,   /* CV_SymKind */
       LF_FUNC_ID = 0x1601, LF_MFUNC_ID = 0x1602 };                                 /* CV_LeafKind */

typedef struct {
  Buf s[RDI_SECTIONS];
  Names strings, nodes, files;
  Buf pairs;                 /* name map entries: (kind, string, index) */
  Buf scopes;                /* what each scope covers: (first, ~end, scope) */
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

/* What an inlined call covers, as pairs added to `out`, from the annotations of its
   S_INLINESITE record; offsets count from the procedure's first byte, `base`. An annotation
   moves the code offset (a line starts there), or gives the length of the code at the offset,
   which ends a piece; a piece runs from the first offset since the last piece ended, and one
   that starts where the piece before ended continues it. Offsets left without a length are a
   piece up to the last of them. This is how LLVM reads and writes them (llvm-pdbutil's "code"
   and "code end", MCCodeView.cpp). RAD (cv_c13_inline_site_decoder_step) also adds the length
   of annotation 12 to the offset, and sibling calls then overlap (lessons, L26). Nothing is
   added when the annotations are malformed or of a kind not decoded. NOT YET decoded: code
   moved out of the procedure (annotation 2), an offset that is not a step (annotation 1) and
   S_INLINESITE2; no compiler here writes them, and LLVM and RAD read the first two differently. */
static void inline_pieces(Buf *out, PdbSpan a, uint64_t base)
{
  uint64_t start = out->n;
  uint32_t off = 0, lo = 0;
  bool open = false;                                     /* an offset was given since the last piece ended: `lo` */
  for (uint32_t at = 0, op = 1, x, y; op != 0;) {
    uint64_t end = 0;                                    /* where a piece ends, when this annotation ends one */
    x = y = op = 0;                                      /* the end of the record is annotation 0, the end */
    bool ok = at == a.n || (operand(a, &at, &op) && (op == 0 || operand(a, &at, &x)) && (op != 12 || operand(a, &at, &y)));
    if (op == 3) off += x;                               /* ChangeCodeOffset */
    else if (op == 11) off += x & 15;                    /* ChangeCodeOffsetAndLineOffset */
    else if (op == 12) off += y;                         /* ChangeCodeLengthAndCodeOffset */
    else if (op != 0 && op != 4 && op != 5 && op != 6) ok = false;   /* ChangeCodeLength, ChangeFile and ChangeLineOffset move nothing */
    if (!ok) {
      out->n = start;
      return;
    }
    if (!open && op != 0 && op != 5 && op != 6) lo = off, open = true;
    if (op == 4 || op == 12) end = base + off + x;
    else if (op == 0 && open) end = base + off;
    if (op == 4) off += x;                               /* the next offset counts from this end; after annotation 12 it does not */
    if (end == 0) continue;
    if (out->n > start && *LAST(*out, uint64_t) == base + lo) *LAST(*out, uint64_t) = end;
    else pair(out, base + lo, end);
    open = false;
  }
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
  add(&o->s[RDI_INDEX_RUNS], 4);
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
  Buf lines = {0}, maps = {0}, ranges = {0}, scratch = {0}, open = {0}, links = {0}, id_ats = {0};
  /* index 0 of every table is the record that means "none" */
  static const int tables[] = {RDI_BINARY_SECTIONS, RDI_FILE_PATH_NODES, RDI_SOURCE_FILES, RDI_LINE_TABLES, RDI_SOURCE_LINE_MAPS, RDI_UNITS, RDI_SCOPES, RDI_PROCEDURES, RDI_INLINE_SITES};
  static const uint8_t table_size[] = {sizeof(RdiSection), sizeof(RdiPathNode), sizeof(RdiSourceFile), sizeof(RdiLineTable), sizeof(RdiSourceLineMap), sizeof(RdiUnit), sizeof(RdiScope), sizeof(RdiSymbol), sizeof(RdiInlineSite)};
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
  uint32_t first_id;
  uint8_t *id_stream = pdb_ids(&pdb, &ids, &first_id);
  for (uint32_t at = 0, next = 0; pdb_symbol(ids, &next, &(uint16_t){0}, &(PdbSpan){0}); at = next) *(uint32_t *)add(&id_ats, 4) = at;
  uint16_t kind;
  uint64_t order = 0;
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
    lines.n = 0;
    for (PdbLineCursor c = {0}; pdb_line_block(&pdb, c13, &c, &b);) {
      uint32_t file = source_file(&o, &b, ((RdiPathNode *)o.s[RDI_FILE_PATH_NODES].p)[unit.object_path].parent);
      for (uint32_t j = 0; j < b.count; j += 1) {
        uint64_t voff = b.base + le32(b.lines + 8 * j), line = le32(b.lines + 8 * j + 4) & 0xffffff;
        rec(&lines, voff, 1ull << 63 | order++, file, line);
        rec(&maps, file, line, voff, 0);
      }
      rec(&lines, b.opl, order++, 0, 0);
    }
    uint64_t n, *l = sorted(&lines, &n);
    if (n) {
      unit.line_table = COUNT(o.s[RDI_LINE_TABLES], RdiLineTable);
      RdiLineTable t = {.voffs_base = COUNT(o.s[RDI_LINE_VOFFS], uint64_t), .lines_base = COUNT(o.s[RDI_LINE_LINES], RdiLine)};
      for (uint64_t i = 0; i < n; i += 1) {
        *(uint64_t *)add(&o.s[RDI_LINE_VOFFS], 8) = l[4 * i];
        *(RdiLine *)add(&o.s[RDI_LINE_LINES], sizeof(RdiLine)) = (RdiLine){(uint32_t)l[4 * i + 2], (uint32_t)l[4 * i + 3]};
        t.lines_count += 1;
      }
      *(uint64_t *)add(&o.s[RDI_LINE_VOFFS], 8) = UINT64_MAX;   /* RAD's reader looks one past the last */
      *(RdiLineTable *)add(&o.s[RDI_LINE_TABLES], sizeof t) = t;
    }

    /* Procedures and scopes (rdi_from_codeview.c, "symbols pass 2"): a procedure opens its root
       scope; a block or an inlined call opens a scope inside the open one; S_END and
       S_INLINESITE_END close the open one. A thunk is a procedure that opens nothing. `open` is
       the stack of open scopes; every module leaves it empty. */
    unit.procs_first = COUNT(o.s[RDI_PROCEDURES], RdiSymbol);
    uint64_t base = 0;                                                      /* where the procedure being read starts */
    for (uint32_t at = 0; pdb_symbol(syms, &at, &kind, &d);) {
      uint32_t top = open.n ? *LAST(open, uint32_t) : 0, made = 0, inside = ((RdiScope *)o.s[RDI_SCOPES].p)[top].proc;
      if ((kind == S_END || kind == S_INLINESITE_END) && open.n) open.n -= 4;
      else if ((kind == S_GPROC32 || kind == S_LPROC32) && d.n >= 35) {   /* CV_SymProc32 */
        uint64_t voff = base = pdb_voff(&pdb, le16(d.p + 32), le32(d.p + 28));
        PdbSpan name = pdb_string(d, &(uint32_t){35});
        uint32_t proc = procedure(&o, (RdiSymbol){.name = intern(&o.strings, name.p, name.n), .container_flags = kind == S_GPROC32 ? 0x80 : 0}, voff, voff + le32(d.p + 12));
        rec(&links, voff, 0, proc, 0);
        made = COUNT(o.s[RDI_SCOPES], RdiScope) - 1;
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
        inline_pieces(&o.s[RDI_SCOPE_VOFFS], (PdbSpan){d.p + 12, d.n - 12}, base);
        /* its name is in the id record it names: two type ids, then the name (CV_LeafFuncId, CV_LeafMFuncId) */
        uint32_t id = le32(d.p + 8) - first_id, id_at = id < COUNT(id_ats, uint32_t) ? ((uint32_t *)id_ats.p)[id] : ids.n;
        PdbSpan name = pdb_symbol(ids, &id_at, &kind, &d) && (kind == LF_FUNC_ID || kind == LF_MFUNC_ID) ? pdb_string(d, &(uint32_t){8}) : (PdbSpan){0};
        made = scope(&o, inside, top, COUNT(o.s[RDI_INLINE_SITES], RdiInlineSite), start);
        *(RdiInlineSite *)add(&o.s[RDI_INLINE_SITES], sizeof(RdiInlineSite)) = (RdiInlineSite){.name = intern(&o.strings, name.p, name.n)};
      }
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
    if (voff == 0) continue;
    PdbSpan name = pdb_string(d, &(uint32_t){10});
    rec(&links, voff, ++order, intern(&o.strings, name.p, name.n), le32(d.p) & 3);   /* code or function */
  }
  free(globals);
  free(id_stream);
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
    uint32_t path = normal_path(&o, ((RdiSourceFile *)o.s[RDI_SOURCE_FILES].p)[file].path_node, &scratch);
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
  Buf *rest[] = {&o.strings.slots, &o.nodes.keys, &o.nodes.at, &o.nodes.slots, &o.files.keys, &o.files.at, &o.files.slots, &o.pairs, &o.scopes, &lines, &maps, &ranges, &scratch, &open, &links, &id_ats};
  for (unsigned i = 0; i < sizeof rest / sizeof *rest; i += 1) free(rest[i]->p);
  pdb_close(&pdb);
  *out_size = file.n;
  return file.p;
}
