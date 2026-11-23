/* Where a variable lives (DWARF location expressions) and how Zig's std.fmt "{any}" prints it. */
#ifndef ZDB_VALUE_H
#define ZDB_VALUE_H
#include "dwarf.h"
#include "target.h"

typedef struct { char *s; size_t n, cap; } Out;
void out_add(Out *o, const char *fmt, ...);

/* The variable named `name` visible at the stop (innermost scope first), or 0. */
uint32_t var_at(const Dwarf *d, uint64_t pc, const char *name);
uint32_t global_named(const Dwarf *d, const char *name);          /* a variable outside any function, or 0 */
/* Prints the variable's value as std.fmt "{any}" would; false (and a reason in o) when it cannot. */
bool print_var(const Dwarf *d, const Target *t, const Regs *r, uint32_t var, Out *o);
bool print_text(const Dwarf *d, const Target *tg, const Regs *r, uint32_t var, Out *o);   /* a []const u8 as "{s}" prints it */
/* the variable lives at rsp + *off at pc (DW_OP_breg7 off: the self-hosted backend's locals) */
bool rsp_slot(const Dwarf *d, uint32_t var, uint64_t pc, int64_t *off);
uint64_t type_size(const Dwarf *d, uint32_t type);
#endif
