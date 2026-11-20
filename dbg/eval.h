/* What a name means at a stack frame, and where its bytes are (the knowledge base, 05; RAD's
   e_oplist_from_location). The first part of the evaluator: names
   only, no expressions yet. */
#ifndef DBG_EVAL_H
#define DBG_EVAL_H
#include "engine.h"

enum { EVAL_NONE,       /* no such name here, or its place is not known at this instruction */
       EVAL_MEMORY,     /* at: its address in the target */
       EVAL_REGISTER,   /* at: 0 to 15, the general register that holds it (its low bytes when it is smaller) */
       EVAL_CONSTANT }; /* at: its offset in the module's location constants */
typedef struct { int kind; const EngineModule *module; uint32_t type; uint64_t at; } EvalPlace;   /* type: in the module's debug info */

/* The variable `name` as the frame with these registers sees it: a local or parameter of the scopes around
   its instruction, the innermost first; else a global or a named constant of its module, else of the other modules.
   called: the frame is not the innermost: it waits in a call, and its instruction is the one that ends at its
   return address. (RAD looks at the return address itself, and adds the scope one byte before: after a call that
   ends or begins a block it finds the other block's variable of the same name. gdb and lldb do as here.) */
EvalPlace eval_name(const Engine *e, const TargetRegs *frame, bool called, const char *name, uint64_t len);
/* The first n bytes of what is there. */
bool eval_read(const Engine *e, const TargetRegs *frame, EvalPlace p, void *bytes, uint32_t n);
#endif
