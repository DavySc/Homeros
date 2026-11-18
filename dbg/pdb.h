/* What the converter needs from a PDB, read through the container (msf.h). Field layouts follow
   RAD's pdb/pdb.h. Everything read from the file is bounds-checked here, so callers get either
   a whole record or none. */
#ifndef DBG_PDB_H
#define DBG_PDB_H
#include "msf.h"

typedef struct { const uint8_t *p; uint32_t n; } PdbSpan;
typedef struct {
  Msf msf;
  uint8_t *dbi, *sec;         /* the DBI stream and the image's section headers, owned */
  PdbSpan mods, contribs;     /* parts of dbi: module records, section contributions */
  uint32_t contrib_size;      /* bytes per contribution */
  uint32_t sections;
} Pdb;

typedef struct { uint16_t sec, mod; uint32_t off, size; } PdbContrib;
typedef struct {
  PdbSpan name, obj;          /* the object file, and the library or object it came from */
  PdbContrib first;           /* its first contribution */
  uint16_t stream;            /* its symbols and lines; 0xffff when it has none */
  uint32_t sym_size, c11_size, c13_size;
} PdbModule;
typedef struct { PdbSpan name; uint32_t vsize, voff, fsize, foff, flags; } PdbSection;

bool pdb_open(Pdb *p, const void *data, uint64_t size);   /* false: no debug info to convert */
void pdb_close(Pdb *p);
/* the module record at *at (start at 0); moves *at to the next. False at the end. */
bool pdb_module(const Pdb *p, uint32_t *at, PdbModule *m);
static inline uint32_t pdb_contribs(const Pdb *p) { return p->contribs.n / p->contrib_size; }
PdbContrib pdb_contrib(const Pdb *p, uint32_t i);          /* i < pdb_contribs(p) */
PdbSection pdb_section(const Pdb *p, uint32_t number);     /* 1-based; all zero when out of range */
/* section:offset as an offset from the image base, 0 when there is no such section */
uint64_t pdb_voff(const Pdb *p, uint16_t sec, uint32_t off);
#endif
