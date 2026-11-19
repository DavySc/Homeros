/* One step up the stack of a 64-bit Windows program: from a thread's registers inside a
   function to its registers in the caller, just after the call. The rules are the system's own
   (RtlVirtualUnwind, as published in .NET's unwinder_amd64.cpp): the function's entry in the
   module's exception table names its unwind codes; inside an epilogue, which the table does not
   describe, the instructions themselves are read (the system also reads the epilogue's first
   instruction, the one that frees the frame; before it has run the codes give the same answer). */
#ifndef DBG_UNWIND_H
#define DBG_UNWIND_H
#include "target.h"

/* A module: where it is mapped, and its exception table as it lies in memory (`count` entries of 12 bytes, sorted). */
typedef struct { uint64_t base, size; uint8_t *table; uint32_t count; } UnwindModule;

/* Reads the module mapped at base out of the target's memory (read: target_read, or a test's stand-in
   for it); the caller frees m->table. False when there is no 64-bit program image at base. */
bool unwind_module(TargetRead *read, Target *t, uint64_t base, UnwindModule *m);

/* m: the module that holds regs->rip, or null (then the function made no frame: only the return
   address is popped). False when memory cannot be read or the table makes no sense; regs are
   then worthless. Only the general registers, rip and the stack pointer are followed. */
bool unwind_step(TargetRead *read, Target *t, const UnwindModule *m, TargetRegs *regs);
#endif
