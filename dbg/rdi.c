#include "rdi.h"
#include <string.h>

/* element sizes; a section this reader does not interpret yet is bytes */
static const uint8_t elem_size[RDI_SECTIONS] = {
  [RDI_NULL] = 1, [RDI_TOP_LEVEL_INFO] = 1, [RDI_STRING_DATA] = 1, [RDI_STRING_TABLE] = 4,
  [RDI_INDEX_RUNS] = 4, [RDI_BINARY_SECTIONS] = sizeof(RdiSection), [RDI_FILE_PATH_NODES] = sizeof(RdiPathNode),
  [RDI_SOURCE_FILES] = sizeof(RdiSourceFile), [RDI_LINE_TABLES] = sizeof(RdiLineTable),
  [RDI_LINE_VOFFS] = 8, [RDI_LINE_LINES] = sizeof(RdiLine), [RDI_LINE_COLUMNS] = 4,
  [RDI_SOURCE_LINE_MAPS] = sizeof(RdiSourceLineMap), [RDI_SOURCE_LINE_NUMS] = 4,
  [RDI_SOURCE_LINE_RANGES] = 4, [RDI_SOURCE_LINE_VOFFS] = 8, [RDI_UNITS] = sizeof(RdiUnit),
  [RDI_UNIT_VMAP] = sizeof(RdiVMap), [RDI_NAMESPACES] = 1, [RDI_TYPE_NODES] = sizeof(RdiType), [RDI_UDTS] = sizeof(RdiUdt),
  [RDI_MEMBERS] = sizeof(RdiMember), [RDI_ENUM_MEMBERS] = sizeof(RdiEnumMember), [RDI_SCOPES] = sizeof(RdiScope), [RDI_SCOPE_VOFFS] = 8,
  [RDI_SCOPE_VMAP] = sizeof(RdiVMap), [RDI_INLINE_SITES] = sizeof(RdiInlineSite), [RDI_GLOBALS] = sizeof(RdiSymbol),
  [RDI_GLOBAL_VMAP] = sizeof(RdiVMap), [RDI_THREAD_VARS] = sizeof(RdiSymbol),
  [RDI_CONSTANTS] = sizeof(RdiSymbol), [RDI_PROCEDURES] = sizeof(RdiSymbol),
  [RDI_LOCALS] = sizeof(RdiSymbol), [RDI_LOCATION_BYTECODE] = 1, [RDI_LOCATION_CONSTANTS] = 8,
  [RDI_LOCATION_SETS] = sizeof(RdiLocationCase), [RDI_MD5] = 1, [RDI_SHA1] = 1, [RDI_SHA256] = 1, [RDI_TIMESTAMPS] = 8,
  [RDI_NAME_MAPS] = sizeof(RdiNameMap), [RDI_NAME_MAP_BUCKETS] = sizeof(RdiBucket),
  [RDI_NAME_MAP_NODES] = sizeof(RdiNameNode),
};

typedef struct { uint64_t magic; uint32_t version, sections_off, sections_count; } Header;
typedef struct { uint32_t encoding, pad; uint64_t off, encoded_size, unpacked_size; } Section;

static const uint64_t zero[8];   /* the zero record: as large as the largest element (RdiUnit) */

bool rdi_open(Rdi *r, const void *data, uint64_t size)
{
  const Header *h = data;
  /* an absent section is zero elements of the zero record, never a null pointer */
  for (int k = 0; k < RDI_SECTIONS; k += 1) r->s[k] = (RdiSpan){zero, 0};
  if (size < sizeof *h || h->magic != 0x0000676264646172u || h->version != 23) return false;
  if (h->sections_off % 8 != 0 || h->sections_off > size ||
      h->sections_count > (size - h->sections_off) / sizeof(Section)) return false;
  const Section *sec = (const Section *)((const uint8_t *)data + h->sections_off);
  for (uint32_t k = 0; k < h->sections_count && k < RDI_SECTIONS; k += 1) {
    /* compressed sections are not read: the converter is asked for unpacked files */
    if (sec[k].encoding != 0) return false;
    if (sec[k].off > size || sec[k].encoded_size > size - sec[k].off) return false;
    /* typed access needs the element's alignment; 8 covers every element */
    if (elem_size[k] > 1 && sec[k].off % 8 != 0) return false;
    r->s[k].p = (const uint8_t *)data + sec[k].off;
    r->s[k].n = sec[k].encoded_size / elem_size[k];
  }
  return true;
}

const void *rdi_elem(const Rdi *r, int k, uint64_t i)
{
  return i < r->s[k].n ? (const uint8_t *)r->s[k].p + i * elem_size[k] : (const void *)zero;
}

RdiSpan rdi_run(const Rdi *r, int k, uint64_t first, uint64_t count)
{
  uint64_t n = r->s[k].n;
  if (first > n) first = n;
  if (count > n - first) count = n - first;
  return (RdiSpan){(const uint8_t *)r->s[k].p + first * elem_size[k], count};
}

RdiSpan rdi_str(const Rdi *r, uint32_t idx)
{
  /* string idx is [table[idx], table[idx+1]) in the string data; the last one runs to the end */
  uint64_t n = r->s[RDI_STRING_TABLE].n;
  if (idx >= n) return (RdiSpan){zero, 0};
  uint64_t off = *rdi_at(r, RDI_STRING_TABLE, uint32_t, idx);
  uint64_t opl = idx + 1 < n ? *rdi_at(r, RDI_STRING_TABLE, uint32_t, idx + 1) : r->s[RDI_STRING_DATA].n;
  if (off > opl) off = opl;
  return rdi_run(r, RDI_STRING_DATA, off, opl - off);
}

/* First i in [0,n) with key(i) >= key, or n. Keys are `width` bytes at the start of each
   `stride`-byte element and ascend. The one search behind every sorted table in the format. */
static uint64_t lower_bound(const void *keys, uint64_t n, size_t stride, size_t width, uint64_t key)
{
  uint64_t lo = 0, hi = n;
  while (lo < hi) {
    uint64_t mid = lo + (hi - lo) / 2, k = 0;
    memcpy(&k, (const uint8_t *)keys + mid * stride, width);
    if (k < key) lo = mid + 1; else hi = mid;
  }
  return lo;
}

/* Last i whose key is <= voff, or n when voff is outside [key(0), key(n-1)): the last entry of
   these tables only marks the end. Among entries with equal keys the last one is the one whose
   range holds voff; RAD answers with the first when voff equals the key exactly
   (rdi_line_info_idx_range_from_voff), which hides the line behind a zero-length entry. */
static uint64_t floor_index(const uint64_t *keys, uint64_t n, size_t stride, uint64_t voff)
{
  if (voff == UINT64_MAX) return n;
  uint64_t above = lower_bound(keys, n, stride, 8, voff + 1);
  return above == 0 || above == n ? n : above - 1;
}

uint32_t rdi_vmap(const Rdi *r, int k, uint64_t voff)
{
  uint64_t i = floor_index(r->s[k].p, r->s[k].n, sizeof(RdiVMap), voff);
  return (uint32_t)rdi_at(r, k, RdiVMap, i)->idx;
}

RdiLine rdi_line_from_voff(const Rdi *r, uint64_t voff)
{
  return rdi_line_in(r, rdi_at(r, RDI_UNITS, RdiUnit, rdi_vmap(r, RDI_UNIT_VMAP, voff))->line_table, voff);
}

RdiLine rdi_line_in(const Rdi *r, uint32_t line_table, uint64_t voff)
{
  const RdiLineTable *t = rdi_at(r, RDI_LINE_TABLES, RdiLineTable, line_table);
  RdiSpan voffs = rdi_run(r, RDI_LINE_VOFFS, t->voffs_base, t->lines_count);
  uint64_t i = floor_index(voffs.p, voffs.n, 8, voff);
  return *rdi_at(r, RDI_LINE_LINES, RdiLine, i < voffs.n ? t->lines_base + i : UINT64_MAX);
}

RdiSpan rdi_voffs_from_line(const Rdi *r, uint32_t file, uint32_t line)
{
  const RdiSourceFile *f = rdi_at(r, RDI_SOURCE_FILES, RdiSourceFile, file);
  const RdiSourceLineMap *m = rdi_at(r, RDI_SOURCE_LINE_MAPS, RdiSourceLineMap, f->line_map);
  RdiSpan nums = rdi_run(r, RDI_SOURCE_LINE_NUMS, m->nums_base, m->line_count);
  if (nums.n == 0) return (RdiSpan){zero, 0};
  /* the first line at or after the one asked for; past the last line, the last line
     (rdi_line_voffs_from_num does the same) */
  uint64_t i = lower_bound(nums.p, nums.n, 4, 4, line);
  if (i == nums.n) i -= 1;
  /* ranges holds line_count + 1 offsets into this map's voffs */
  RdiSpan rng = rdi_run(r, RDI_SOURCE_LINE_RANGES, (uint64_t)m->ranges_base + i, 2);
  if (rng.n < 2) return (RdiSpan){zero, 0};
  const uint32_t *b = rng.p;
  if (b[0] > b[1] || b[1] > m->voff_count) return (RdiSpan){zero, 0};
  return rdi_run(r, RDI_SOURCE_LINE_VOFFS, (uint64_t)m->voffs_base + b[0], b[1] - b[0]);
}

uint64_t rdi_path(const Rdi *r, uint32_t node, char *buf, uint64_t cap)
{
  /* two walks to the root: one for the length, one to write the parts from the back. A file
     may link nodes in a ring, so a walk takes at most as many steps as there are nodes. */
  uint64_t len = 0, at = 0;
  for (int pass = 0; pass < 2; pass += 1) {
    uint32_t k = node;
    for (uint64_t steps = r->s[RDI_FILE_PATH_NODES].n; k != 0 && steps > 0; steps -= 1) {
      const RdiPathNode *n = rdi_at(r, RDI_FILE_PATH_NODES, RdiPathNode, k);
      RdiSpan name = rdi_str(r, n->name);
      if (pass == 0) len += name.n + 1;
      else {
        at -= name.n;
        for (uint64_t i = 0; i < name.n && at + i < cap; i += 1) buf[at + i] = ((const char *)name.p)[i];
        if (at > 0 && --at < cap) buf[at] = '/';
      }
      k = n->parent;
    }
    if (len > 0 && pass == 0) at = len -= 1;
  }
  return len;
}

uint64_t rdi_hash(const void *bytes, uint64_t len)   /* djb2, and 0 for the empty string */
{
  uint64_t hash = len ? 5381 : 0;
  for (uint64_t i = 0; i < len; i += 1) hash = hash * 33 + ((const uint8_t *)bytes)[i];
  return hash;
}

RdiSpan rdi_matches(const Rdi *r, uint32_t kind, const char *name, uint64_t len)
{
  const RdiNameMap *m = rdi_at(r, RDI_NAME_MAPS, RdiNameMap, kind);
  RdiSpan buckets = rdi_run(r, RDI_NAME_MAP_BUCKETS, m->bucket_base, m->bucket_count);
  if (buckets.n == 0) return (RdiSpan){zero, 0};
  const RdiBucket *b = (const RdiBucket *)buckets.p + rdi_hash(name, len) % buckets.n;
  RdiSpan nodes = rdi_run(r, RDI_NAME_MAP_NODES, (uint64_t)m->node_base + b->first_node, b->node_count);
  for (uint64_t i = 0; i < nodes.n; i += 1) {
    const RdiNameNode *node = (const RdiNameNode *)nodes.p + i;
    RdiSpan s = rdi_str(r, node->string);
    if (s.n != len || memcmp(s.p, name, len) != 0) continue;
    /* one match is stored in the node itself, more as a run of indices */
    if (node->match_count == 1) return (RdiSpan){&node->match, 1};
    return rdi_run(r, RDI_INDEX_RUNS, node->match, node->match_count);
  }
  return (RdiSpan){zero, 0};
}
