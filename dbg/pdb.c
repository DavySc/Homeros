#include "pdb.h"
#include "le.h"
#include <stdlib.h>
#include <string.h>

enum { INFO_STREAM = 1, INFO_HEADER = 28 /* version, signature, age, GUID */, DBI_STREAM = 3, IPI_STREAM = 4, DBI_HEADER = 64, MODULE_HEADER = 64, SECTION_HEADER = 40,
       SECTION_HEADER_SLOT = 5 /* PDB_DbiStream_SECTION_HEADER */,
       C13_LINES = 0xf2, C13_CHECKSUMS = 0xf4, C13_INLINEE_LINES = 0xf6 };

/* [off, off+n) of s, cut to what s holds */
static PdbSpan part(PdbSpan s, uint64_t off, uint64_t n)
{
  if (s.n == 0) return s;       /* an absent stream is a null pointer, which takes no offset */
  if (off > s.n) off = s.n;
  if (n > s.n - off) n = s.n - off;
  return (PdbSpan){s.p + off, (uint32_t)n};
}

PdbSpan pdb_string(PdbSpan s, uint32_t *at)
{
  uint32_t start = *at < s.n ? *at : s.n, end = start;
  while (end < s.n && s.p[end] != 0) end += 1;
  *at = end < s.n ? end + 1 : end;
  return part(s, start, end - start);
}

/* The number of the stream called `name`, from the table in the info stream
   (pdb_named_stream_table_from_info): name bytes, a count, a capacity, two bit sets, then
   count pairs of (offset of a name, stream number). 0xffff when there is none. */
static uint16_t named_stream(PdbSpan info, const char *name)
{
  PdbSpan names = part(info, INFO_HEADER + 4, info.n >= INFO_HEADER + 4 ? le32(info.p + INFO_HEADER) : 0);
  uint64_t at = INFO_HEADER + 4 + (uint64_t)names.n;
  uint32_t count = part(info, at, 4).n == 4 ? le32(info.p + at) : 0;
  at += 8;                                               /* count, capacity */
  for (int set = 0; set < 2; set += 1) at += 4 + 4 * (uint64_t)(part(info, at, 4).n == 4 ? le32(info.p + at) : 0);
  PdbSpan pairs = part(info, at, 8 * (uint64_t)count);
  for (uint32_t i = 0; i + 8 <= pairs.n; i += 8) {
    uint32_t off = le32(pairs.p + i);
    PdbSpan s = pdb_string(names, &off);
    if (s.n == strlen(name) && memcmp(s.p, name, s.n) == 0) return le16(pairs.p + i + 4);
  }
  return 0xffff;
}

bool pdb_open(Pdb *p, const void *data, uint64_t size)
{
  *p = (Pdb){.contrib_size = 1};
  if (!msf_open(&p->msf, data, size)) return false;
  uint32_t n;
  p->dbi = msf_stream(&p->msf, DBI_STREAM, &n);
  if (n < DBI_HEADER) {
    pdb_close(p);
    return false;
  }
  /* after the header: modules, contributions, section map, file info, type server map,
     edit-and-continue info, then the numbers of the optional streams (PDB_DbiHeader) */
  PdbSpan dbi = {p->dbi, n};
  p->machine = le16(p->dbi + 58);
  uint64_t at = DBI_HEADER, sizes[6];
  for (int i = 0; i < 6; i += 1) sizes[i] = le32(p->dbi + (i < 5 ? 24 + 4 * i : 52));
  p->mods = part(dbi, at, sizes[0]);
  PdbSpan contribs = part(dbi, at + sizes[0], sizes[1]);
  for (int i = 0; i < 6; i += 1) at += sizes[i];
  PdbSpan slots = part(dbi, at, le32(p->dbi + 48));
  /* contributions start with a version that gives the record size (PDB_DbiSCVersion_1, _2) */
  uint32_t version = contribs.n >= 4 ? le32(contribs.p) : 0;
  p->contrib_size = version == 0xeffe0000u + 19970605u ? 28 : version == 0xeffe0000u + 20140516u ? 32 : 1;
  if (p->contrib_size != 1) p->contribs = part(contribs, 4, contribs.n);
  if (slots.n >= 2 * SECTION_HEADER_SLOT + 2) {
    p->sec = msf_stream(&p->msf, le16(slots.p + 2 * SECTION_HEADER_SLOT), &n);
    p->sections = n / SECTION_HEADER;
  }
  /* the string table: a signature, a version, the size of the strings, the strings (PDB_StrtblHeader) */
  uint8_t *info = msf_stream(&p->msf, INFO_STREAM, &n);
  uint16_t names = named_stream((PdbSpan){info, n}, "/names");
  free(info);
  p->names = msf_stream(&p->msf, names, &n);
  p->strings = part((PdbSpan){p->names, n}, 12, n >= 12 ? le32(p->names + 8) : 0);
  return true;
}

void pdb_close(Pdb *p)
{
  free(p->dbi);
  free(p->sec);
  free(p->names);
  msf_close(&p->msf);
  p->dbi = p->sec = p->names = 0;
}

static PdbContrib contrib_at(const uint8_t *c)   /* PDB_DbiSC40 */
{
  return (PdbContrib){.sec = le16(c), .off = le32(c + 4), .size = le32(c + 8), .mod = le16(c + 16)};
}

bool pdb_module(const Pdb *p, uint32_t *at, PdbModule *m)
{
  if (p->mods.n - *at < MODULE_HEADER) return false;   /* *at <= mods.n always */
  const uint8_t *h = p->mods.p + *at;                   /* PDB_DbiCompUnitHeader */
  *m = (PdbModule){.first = contrib_at(h + 4), .stream = le16(h + 34), .sym_size = le32(h + 36),
                   .c11_size = le32(h + 40), .c13_size = le32(h + 44)};
  *at += MODULE_HEADER;
  m->name = pdb_string(p->mods, at);
  m->obj = pdb_string(p->mods, at);
  *at = (*at + 3) / 4 * 4;                              /* records are 4-aligned */
  if (*at > p->mods.n) *at = p->mods.n;
  return true;
}

uint8_t *pdb_globals(const Pdb *p, PdbSpan *syms)
{
  uint8_t *stream = msf_stream(&p->msf, le16(p->dbi + 20), &syms->n);   /* PDB_DbiHeader.sym_sn */
  syms->p = stream;
  return stream;
}

uint8_t *pdb_ids(const Pdb *p, PdbSpan *records, uint32_t *first)
{
  uint32_t n;
  uint8_t *stream = msf_stream(&p->msf, IPI_STREAM, &n);
  bool has = n >= 20;          /* PDB_TpiHeader: version, header size, first id, one past the last, size of the records */
  *first = has ? le32(stream + 8) : 0;
  *records = has ? part((PdbSpan){stream, n}, le32(stream + 4), le32(stream + 16)) : (PdbSpan){0};
  return stream;
}

bool pdb_symbol(PdbSpan syms, uint32_t *at, uint16_t *kind, PdbSpan *data)
{
  if (syms.n - *at < 4) return false;                    /* *at <= syms.n always */
  uint16_t size = le16(syms.p + *at);                    /* counts the kind and the data (CV_RecHeader) */
  *kind = le16(syms.p + *at + 2);
  *data = part(syms, (uint64_t)*at + 4, size < 2 ? 0 : size - 2);
  *at = (uint32_t)(data->p - syms.p) + data->n;
  return true;
}

uint8_t *pdb_module_stream(const Pdb *p, const PdbModule *m, PdbSpan *syms, PdbSpan *c13)
{
  uint32_t n;
  uint8_t *stream = msf_stream(&p->msf, m->stream, &n);
  /* symbols (after a 4-byte signature), old-style lines, then C13 */
  *syms = part((PdbSpan){stream, n}, 4, m->sym_size < 4 ? 0 : m->sym_size - 4);
  *c13 = part((PdbSpan){stream, n}, (uint64_t)m->sym_size + m->c11_size, m->c13_size);
  return stream;
}

/* the subsection at *next: kind, size, data, padded to 4 (cv_c13_sub_section_from_data) */
static bool subsection(PdbSpan c13, uint32_t *next, uint32_t *kind, PdbSpan *data)
{
  if (c13.n - *next < 8) return false;
  *kind = le32(c13.p + *next);
  *data = part(c13, (uint64_t)*next + 8, le32(c13.p + *next + 4));
  uint64_t end = (uint64_t)(data->p - c13.p) + data->n;
  *next = (end + 3) / 4 * 4 < c13.n ? (uint32_t)((end + 3) / 4 * 4) : c13.n;
  return true;
}

bool pdb_line_block(const Pdb *p, PdbSpan c13, PdbLineCursor *c, PdbLineBlock *b)
{
  uint32_t kind;
  PdbSpan data;
  if (!c->started) {   /* file names come from the module's checksum subsection, wherever it is */
    for (uint32_t next = 0; subsection(c13, &next, &kind, &data);) if (kind == C13_CHECKSUMS) c->sums = data;
    c->started = true;
  }
  /* a lines subsection: section offset, section, flags, length; then per file: the offset of
     its checksum, a line count, a byte size, the lines, the columns (CV_C13File) */
  while (c->sub_opl - c->at < 12) {
    if (!subsection(c13, &c->next, &kind, &data)) return false;
    if (kind != C13_LINES || data.n < 12 || pdb_voff(p, le16(data.p + 4), 0) == 0) continue;
    c->base = pdb_voff(p, le16(data.p + 4), le32(data.p));
    c->opl = c->base + le32(data.p + 8);
    c->item = le16(data.p + 6) & 1 ? 12 : 8;            /* CV_C13SubSecLinesFlag_HasColumns */
    c->at = (uint32_t)(data.p - c13.p) + 12;
    c->sub_opl = (uint32_t)(data.p - c13.p) + data.n;
  }
  const uint8_t *file = c13.p + c->at;
  uint32_t most = (c->sub_opl - c->at - 12) / c->item;
  *b = pdb_file(p, c->sums, le32(file));
  b->base = c->base, b->opl = c->opl, b->lines = file + 12, b->count = le32(file + 4) < most ? le32(file + 4) : most;
  c->at += 12 + c->item * b->count;
  return true;
}

PdbLineBlock pdb_file(const Pdb *p, PdbSpan sums, uint32_t sum_at)
{
  PdbLineBlock b = {0};
  PdbSpan sum = part(sums, sum_at, 6);                 /* CV_C13Checksum: name, length, kind, bytes */
  if (sum.n == 6) {
    b.file = pdb_string(p->strings, &(uint32_t){le32(sum.p)});
    b.sum_kind = sum.p[5];
    b.sum = part(sums, (uint64_t)sum_at + 6, sum.p[4]);
  }
  return b;
}

bool pdb_inlinee(PdbSpan c13, uint32_t id, uint32_t *sum_at, uint32_t *line)
{
  uint32_t kind;
  PdbSpan data;
  /* a signature, then per function: its id, the offset of its file's checksum, its first line
     (CV_C13InlineeSourceLineHeader); with signature 1 also a count and that many more files */
  for (uint32_t next = 0; subsection(c13, &next, &kind, &data);) {
    if (kind != C13_INLINEE_LINES || data.n < 4 || le32(data.p) > 1) continue;
    uint32_t more = 4 * le32(data.p);
    for (uint64_t at = 4; at <= data.n && data.n - at >= 12 + more; at += 12 + more + (more ? 4 * (uint64_t)le32(data.p + at + 12) : 0)) {
      if (le32(data.p + at) != id) continue;
      *sum_at = le32(data.p + at + 4), *line = le32(data.p + at + 8);
      return true;
    }
  }
  return false;
}

PdbContrib pdb_contrib(const Pdb *p, uint32_t i) { return contrib_at(p->contribs.p + (uint64_t)i * p->contrib_size); }

PdbSection pdb_section(const Pdb *p, uint32_t number)
{
  if (number == 0 || number > p->sections) return (PdbSection){0};
  const uint8_t *h = p->sec + (uint64_t)(number - 1) * SECTION_HEADER;   /* COFF_SectionHeader */
  uint32_t len = 0;
  while (len < 8 && h[len] != 0) len += 1;
  return (PdbSection){{h, len}, le32(h + 8), le32(h + 12), le32(h + 16), le32(h + 20), le32(h + 36)};
}

uint64_t pdb_voff(const Pdb *p, uint16_t sec, uint32_t off)
{
  return sec == 0 || sec > p->sections ? 0 : (uint64_t)pdb_section(p, sec).voff + off;
}
