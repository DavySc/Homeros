/* Reader for RAD Debug Info (RDI) files, encoding version 23 (lib_rdi/rdi.h at 5b0d5f67).
   The file is used in place: rdi_open checks every section once, and after that no lookup can
   read outside the file, whatever indices the file holds. */
#ifndef DBG_RDI_H
#define DBG_RDI_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* section kinds, in the file's order (RDI_SectionKind_XList) */
enum {
  RDI_NULL, RDI_TOP_LEVEL_INFO, RDI_STRING_DATA, RDI_STRING_TABLE, RDI_INDEX_RUNS,
  RDI_BINARY_SECTIONS, RDI_FILE_PATH_NODES, RDI_SOURCE_FILES, RDI_LINE_TABLES,
  RDI_LINE_VOFFS, RDI_LINE_LINES, RDI_LINE_COLUMNS, RDI_SOURCE_LINE_MAPS,
  RDI_SOURCE_LINE_NUMS, RDI_SOURCE_LINE_RANGES, RDI_SOURCE_LINE_VOFFS, RDI_UNITS, RDI_UNIT_VMAP,
  RDI_NAMESPACES, RDI_TYPE_NODES, RDI_UDTS, RDI_MEMBERS, RDI_ENUM_MEMBERS, RDI_SCOPES,
  RDI_SCOPE_VOFFS, RDI_SCOPE_VMAP, RDI_INLINE_SITES, RDI_GLOBALS, RDI_GLOBAL_VMAP,
  RDI_THREAD_VARS, RDI_CONSTANTS, RDI_PROCEDURES, RDI_LOCALS, RDI_LOCATION_BYTECODE,
  RDI_LOCATION_CONSTANTS, RDI_LOCATION_SETS, RDI_MD5, RDI_SHA1, RDI_SHA256, RDI_TIMESTAMPS,
  RDI_NAME_MAPS, RDI_NAME_MAP_BUCKETS, RDI_NAME_MAP_NODES, RDI_SECTIONS
};

/* records, laid out as in the file; index 0 of every table is a zero record */
typedef struct { uint64_t voff, idx; } RdiVMap;
typedef struct { uint32_t voffs_base, lines_base, cols_base, lines_count, cols_count; } RdiLineTable;
typedef struct { uint32_t file, line; } RdiLine;
typedef struct { uint32_t name, flags; uint64_t voff, voff_opl, foff, foff_opl; } RdiSection;   /* flags: 1 read, 2 write, 4 execute */
typedef struct { uint32_t name, parent, first_child, next_sibling, source_file; } RdiPathNode;
typedef struct { uint32_t path_node, path, line_map, checksum_kind, checksum; } RdiSourceFile;
typedef struct { uint32_t line_count, voff_count, nums_base, ranges_base, voffs_base; } RdiSourceLineMap;
typedef struct { uint32_t name, compiler, source_path, object_path, archive_path, build_path, language,
                 line_table, procs_first, procs_count, globals_first, globals_count, tvars_first,
                 tvars_count, consts_first, consts_count; } RdiUnit;
typedef struct { uint32_t proc, parent, first_child, next_sibling, voff_first, voff_opl, local_first,
                 local_count, inline_site; } RdiScope;
typedef struct { uint32_t name, type, owner_type, line_table; } RdiInlineSite;
/* A type. What a, b, c hold goes by kind (RDI_TypeNode): below 0x1000, built in: its name. From
   0x1000, made from another (0x1000 modifier: flags 1 const, 2 volatile, 4 restrict; 0x1001
   pointer; 0x1002, 0x1003 references; 0x1004 array; 0x1005 function; 0x1006 method): the type it
   is made from, a count (array elements, parameters), where the parameters' type indices start
   in the index runs. From 0x2000, with a name (0x2000 struct, 0x2001 class, 0x2002 union, 0x2003
   enum, 0x2004 alias, 0x2005 to 0x2008 the same without a definition): name, the type it stands
   on, its record of members. 0xf000 bit field: the type, first bit, bit count. */
typedef struct { uint16_t kind, flags; uint32_t byte_size, a, b, c; } RdiType;
/* A named type's record of members (RDI_UDT): its type, flags (1: the members are an enum's, in the
   enum members), the members' first index and count. The rest is NOT YET written. */
typedef struct { uint32_t self_type; uint8_t flags, container_flags; uint16_t reserved;
                 uint32_t member_first, member_count, file, line, col, container; } RdiUdt;
/* kind (RDI_MemberKind): 1 data, 2 static data, 0x100 method, 0x101 static method, 0x102 virtual
   method, 0x201 base, 0x202 virtual base, 0x300 nested type; off: a data member's or base's offset */
typedef struct { uint16_t kind, pad; uint32_t name, type, off; } RdiMember;
typedef struct { uint32_t name, pad; uint64_t val; } RdiEnumMember;
typedef struct { uint32_t name; uint8_t flags, container_flags; uint16_t reserved; uint64_t location;
                 uint32_t container, type, root_scope, link_name; } RdiSymbol;
/* One case of a location set (RDI_LocationSetElement): where the location holds. A location is 64 bits, its kind (RDI_LocationKind) in
   the top byte: 1 the address is what the bytecode at this offset computes; 3 at register (bits 48 to 55) + offset (16 bits, signed, from
   bit 32); 4 at the address stored there; 5 in the register; 6 at this offset in the image; 7 in the thread's block; 8 in the location
   constants; 9 a set: the count from bit 32, the first case below. */
typedef struct { uint64_t first, opl, location; } RdiLocationCase;
typedef struct { uint32_t bucket_base, node_base, bucket_count, node_count; } RdiNameMap;
typedef struct { uint32_t first_node, node_count; } RdiBucket;
typedef struct { uint32_t string, match_count, match; } RdiNameNode;

enum { RDI_NAMES_GLOBALS = 1, RDI_NAMES_THREAD_VARS, RDI_NAMES_CONSTANTS, RDI_NAMES_PROCEDURES,
       RDI_NAMES_TYPES, RDI_NAMES_LINK_PROCEDURES, RDI_NAMES_SOURCE_PATHS, RDI_NAMES_UNITS, RDI_NAMES_KINDS };

typedef struct { const void *p; uint64_t n; } RdiSpan;   /* n elements */
typedef struct { RdiSpan s[RDI_SECTIONS]; } Rdi;

/* data must be 8-byte aligned and outlive r. False if the file is not a usable RDI. */
bool rdi_open(Rdi *r, const void *data, uint64_t size);

/* element i of section k as T, or the zero record when i is out of range */
#define rdi_at(r, k, T, i) ((const T *)rdi_elem((r), (k), (i)))
const void *rdi_elem(const Rdi *r, int k, uint64_t i);

uint64_t rdi_hash(const void *bytes, uint64_t len);                 /* the hash of name maps */
RdiSpan rdi_str(const Rdi *r, uint32_t idx);                        /* bytes, not zero-terminated */
RdiSpan rdi_run(const Rdi *r, int k, uint64_t first, uint64_t count); /* [first, first+count) clamped to k */

/* The path of a path node (a source file's path_node) into buf, parts joined by '/', not
   terminated. Returns its length; bytes past cap are not written. */
uint64_t rdi_path(const Rdi *r, uint32_t node, char *buf, uint64_t cap);

uint32_t rdi_vmap(const Rdi *r, int vmap_section, uint64_t voff);   /* table index at voff, 0 if none */
RdiLine  rdi_line_from_voff(const Rdi *r, uint64_t voff);
RdiLine  rdi_line_in(const Rdi *r, uint32_t line_table, uint64_t voff);   /* in an inlined call's own table: the line inside the inlined function */
RdiSpan  rdi_voffs_from_line(const Rdi *r, uint32_t file, uint32_t line);   /* uint64_t voffs */
RdiSpan  rdi_matches(const Rdi *r, uint32_t names_kind, const char *name, uint64_t len); /* uint32_t table indices */
#endif
