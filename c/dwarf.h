/* Prototype P1: Zig's own debug info read natively (ELF + DWARF 4/5 with Zig's extensions). */
#ifndef ZDB_DWARF_H
#define ZDB_DWARF_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct { const uint8_t *p; size_t n; } Sec;
typedef struct { int form; uint64_t u; int64_t s; const uint8_t *b; size_t n; } Val;
typedef struct Abbrev { uint64_t code; uint16_t tag; bool kids; const uint8_t *spec; } Abbrev;
typedef struct { uint32_t off, end, version, addr_size, die0; Abbrev *ab; uint32_t nab;
                 uint64_t base, str_off_base, addr_base, loc_base, rng_base;
                 bool zig; /* DW_LANG_Zig: the self-hosted backend (LLVM's units say C99) */ } Unit;
typedef struct { uint32_t off, unit, parent, next; const Abbrev *ab; const uint8_t *attrs; } Die;
typedef struct { uint64_t addr; uint32_t line, file; bool stmt, end; } Row;
typedef struct { const char *name; } File;

typedef struct {
    uint8_t *map; size_t size;
    Sec info, abbrev, str, line_str, line, loc, loclists, ranges, rnglists, addr, str_offsets, frame, eh_frame;
    uint64_t eh_frame_addr;
    Unit *units; uint32_t nunits;
    Die *dies; uint32_t ndies;
    Row *rows; uint32_t nrows;              /* every line row of every unit, sorted by sequence */
    File *files; uint32_t nfiles;           /* rows' file indexes point here */
} Dwarf;

enum { TAG_array = 0x01, TAG_enum = 0x04, TAG_param = 0x05, TAG_lexblock = 0x0b, TAG_member = 0x0d,
       TAG_pointer = 0x0f, TAG_compile_unit = 0x11, TAG_struct = 0x13, TAG_typedef = 0x16, TAG_union = 0x17,
       TAG_variant = 0x19, TAG_inlined = 0x1d, TAG_subrange = 0x21, TAG_base = 0x24, TAG_const = 0x26,
       TAG_enumerator = 0x28, TAG_subprogram = 0x2e, TAG_variant_part = 0x33, TAG_variable = 0x34, TAG_volatile = 0x35 };
enum { AT_location = 0x02, AT_name = 0x03, AT_byte_size = 0x0b, AT_stmt_list = 0x10, AT_low_pc = 0x11,
       AT_high_pc = 0x12, AT_discr = 0x15, AT_discr_value = 0x16, AT_const_value = 0x1c, AT_upper_bound = 0x2f,
       AT_abstract_origin = 0x31, AT_count = 0x37, AT_data_member_location = 0x38, AT_encoding = 0x3e,
       AT_frame_base = 0x40, AT_language = 0x13, AT_type = 0x49, AT_ranges = 0x55, AT_data_bit_offset = 0x6b, AT_bit_size = 0x0d,
       AT_str_offsets_base = 0x72, AT_addr_base = 0x73, AT_rnglists_base = 0x74, AT_loclists_base = 0x8c,
       AT_GNU_vector = 0x2107, AT_ZIG_comptime_value = 0x2cd2, AT_ZIG_sentinel = 0x2ce2 };

bool dwarf_open(Dwarf *d, const char *path);
void dwarf_close(Dwarf *d);
bool die_attr(const Dwarf *d, uint32_t die, int at, Val *v);
const char *die_name(const Dwarf *d, uint32_t die);
bool die_uint(const Dwarf *d, uint32_t die, int at, uint64_t *out);
uint32_t die_ref(const Dwarf *d, uint32_t die, int at);          /* the DIE an attribute refers to, or 0 */
uint32_t die_at(const Dwarf *d, uint32_t off);                   /* the DIE at a .debug_info offset, or 0 */
uint32_t die_kid(const Dwarf *d, uint32_t die);                  /* first child, or 0 */
bool die_has_pc(const Dwarf *d, uint32_t die, uint64_t pc);
uint32_t func_at(const Dwarf *d, uint64_t pc);                   /* the subprogram containing pc, or 0 */
/* the rows for line `line` of the file whose name ends in `file`, as addresses; how many */
uint32_t line_addrs(const Dwarf *d, const char *file, uint32_t line, uint64_t *out, uint32_t room);
const Row *row_at(const Dwarf *d, uint64_t pc);                  /* the row whose range holds pc */
/* the location list or expression of `die` that holds at pc; false when none */
bool die_loc(const Dwarf *d, uint32_t die, uint64_t pc, const uint8_t **expr, size_t *n);
#endif
