/* Where a variable lives (DWARF location expressions) and how Zig's std.fmt "{any}" prints it. */
#ifndef ZDB_VALUE_H
#define ZDB_VALUE_H
#include "dwarf.h"
#include "target.h"

typedef struct { char *s; size_t n, cap; } Out;
void out_add(Out *o, const char *fmt, ...);

/* The variable named `name` visible at the stop (innermost scope first), or 0. */
uint32_t var_at(const Dwarf *d, uint64_t pc, const char *name);
/* Prints the variable's value as std.fmt "{any}" would; false (and a reason in o) when it cannot. */
bool print_var(const Dwarf *d, const Target *t, const Regs *r, uint32_t var, Out *o);
uint64_t type_size(const Dwarf *d, uint32_t type);
#endif
