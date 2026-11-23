/* Edit and continue: Zig's incremental compiler rebuilds the program; the bytes that changed go
   into the stopped process. */
#ifndef ZDB_EC_H
#define ZDB_EC_H
#include "dwarf.h"
#include "target.h"

/* `zig build-exe SRC -fincremental --listen=-`, alive for the whole session */
typedef struct { pid_t pid; int to, from; char name[256], out[512]; } Compiler;
bool zc_start(Compiler *c, const char *zig, const char *src);
/* Rebuilds what changed. 0: built (c->out is the binary's path); 1: compile errors (first message in msg). */
int zc_update(Compiler *c, char *msg, size_t room);
void zc_stop(Compiler *c);

typedef struct { uint64_t code, rodata, data, kept; uint32_t runs; bool refused; char why[256];
                 uint32_t from_line, to_line;
                 uint64_t new_pc; Regs regs; } Patch;        /* new_pc not 0: the top frame goes on with these registers */
/* rsp = rbp - this inside f's body (Zig's self-hosted prologue `push rbp; mov rbp, rsp; sub rsp, N`); 0 for another prologue */
uint64_t frame_size(const Dwarf *d, uint32_t f);
/* Writes into the stopped process what differs between the binary it runs (old) and the new
   one. active: the pc and each return address on the stack. Refuses, writing nothing, when a
   function with an active frame was edited in place or the segments moved. The function the
   process stopped in (active[0]) may be edited: it goes on at the same statement of the new code
   (Visual Studio's rule) when its frame is laid out as before and the statement still exists;
   old_src and new_src (the file's text before and after) say where that statement went. When
   its frame changed, or its registers are lost (lost: a panic stopped it, top is that frame as
   far as memory tells), its call starts again instead; p->why says so. */
bool ec_patch(const Target *t, const Dwarf *old, const Dwarf *new, const uint64_t *active, int nactive, const Regs *top, bool lost,
              const char *old_src, const char *new_src, Patch *p);
#endif
