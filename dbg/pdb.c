#include "pdb.h"
#include "le.h"
#include <stdlib.h>

enum { DBI_STREAM = 3, DBI_HEADER = 64, MODULE_HEADER = 64, SECTION_HEADER = 40,
       SECTION_HEADER_SLOT = 5 /* PDB_DbiStream_SECTION_HEADER */ };

/* [off, off+n) of s, cut to what s holds */
static PdbSpan part(PdbSpan s, uint64_t off, uint64_t n)
{
  if (off > s.n) off = s.n;
  if (n > s.n - off) n = s.n - off;
  return (PdbSpan){s.p + off, (uint32_t)n};
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
  return true;
}

void pdb_close(Pdb *p)
{
  free(p->dbi);
  free(p->sec);
  msf_close(&p->msf);
  p->dbi = p->sec = 0;
}

static PdbContrib contrib_at(const uint8_t *c)   /* PDB_DbiSC40 */
{
  return (PdbContrib){.sec = le16(c), .off = le32(c + 4), .size = le32(c + 8), .mod = le16(c + 16)};
}

/* the zero-terminated string at *at in s; moves *at past it. Empty at the end of s. */
static PdbSpan string_at(PdbSpan s, uint32_t *at)
{
  uint32_t start = *at, end = start;
  while (end < s.n && s.p[end] != 0) end += 1;
  *at = end < s.n ? end + 1 : end;
  return (PdbSpan){s.p + start, end - start};
}

bool pdb_module(const Pdb *p, uint32_t *at, PdbModule *m)
{
  if (p->mods.n - *at < MODULE_HEADER) return false;   /* *at <= mods.n always */
  const uint8_t *h = p->mods.p + *at;                   /* PDB_DbiCompUnitHeader */
  *m = (PdbModule){.first = contrib_at(h + 4), .stream = le16(h + 34), .sym_size = le32(h + 36),
                   .c11_size = le32(h + 40), .c13_size = le32(h + 44)};
  *at += MODULE_HEADER;
  m->name = string_at(p->mods, at);
  m->obj = string_at(p->mods, at);
  *at = (*at + 3) / 4 * 4;                              /* records are 4-aligned */
  if (*at > p->mods.n) *at = p->mods.n;
  return true;
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
