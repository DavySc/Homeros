/* What the converter needs from a PDB, read through the container (msf.h). Field layouts follow
   RAD's pdb/pdb.h. Everything read from the file is bounds-checked here, so callers get either
   a whole record or none. */
#ifndef DBG_PDB_H
#define DBG_PDB_H
#include "msf.h"

typedef struct { const uint8_t *p; uint32_t n; } PdbSpan;
typedef struct {
  Msf msf;
  uint8_t *dbi, *sec, *names; /* the DBI stream, the image's section headers, the string table: owned */
  PdbSpan strings;            /* the string table's bytes, in names */
  PdbSpan mods, contribs;     /* parts of dbi: module records, section contributions */
  uint32_t contrib_size;      /* bytes per contribution */
  uint32_t sections;
  uint16_t machine;           /* COFF machine type: 0x8664 x64, 0x14c x86 */
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
/* The module's stream, which the caller frees, with its symbol records and its line info (C13). */
uint8_t *pdb_module_stream(const Pdb *p, const PdbModule *m, PdbSpan *syms, PdbSpan *c13);

/* The stream of symbol records that belong to no module (public symbols among them), which the
   caller frees. */
uint8_t *pdb_globals(const Pdb *p, PdbSpan *syms);
/* The symbol record at *at of syms (start at 0): its kind and the bytes after the kind. Moves
   *at to the next record. False at the end. */
bool pdb_symbol(PdbSpan syms, uint32_t *at, uint16_t *kind, PdbSpan *data);
/* the zero-terminated string at *at in s; moves *at past it. Empty at or past the end of s. */
PdbSpan pdb_string(PdbSpan s, uint32_t *at);

/* A run of line records for one source file inside one contiguous piece of code. Record i is
   8 bytes: lines + 8*i holds the offset from `base`, then the line number in the low 24 bits. */
typedef struct {
  PdbSpan file, sum;          /* the source file's path; its checksum */
  uint8_t sum_kind;           /* CV_C13ChecksumKind: 0 none, 1 MD5, 2 SHA1, 3 SHA256 */
  uint64_t base, opl;         /* image offsets: where the piece of code starts and ends */
  const uint8_t *lines;
  uint32_t count;
} PdbLineBlock;
typedef struct { PdbSpan sums; uint32_t next, at, sub_opl, item; uint64_t base, opl; bool started; } PdbLineCursor;
/* the next block of c13; *c starts zeroed. False at the end. */
bool pdb_line_block(const Pdb *p, PdbSpan c13, PdbLineCursor *c, PdbLineBlock *b);

PdbSection pdb_section(const Pdb *p, uint32_t number);     /* 1-based; all zero when out of range */
/* section:offset as an offset from the image base, 0 when there is no such section */
uint64_t pdb_voff(const Pdb *p, uint16_t sec, uint32_t off);
#endif
