/* The value of a C expression at a stack frame (the knowledge base, 05). Names come from the debug
   information of the frame's scopes and of the modules; what each operation means is C's own rule, and
   the compiler is the oracle: subjects/zoo.c shows expressions with their address or value.
   Parsed and worked out in one pass over the text; nothing is kept between two evaluations. */
#ifndef DBG_EVAL_H
#define DBG_EVAL_H
#include "engine.h"

/* A type: node `idx` of a module's debug information, or with no module a built-in kind (RDI_TypeKind:
   0x13 int, 0x1e double, ...); with `stars` pointers on top of it. */
typedef struct { const EngineModule *module; uint32_t idx, stars; } EvalType;

enum { EVAL_NONE,       /* nothing: the expression has no meaning here (the error says why) */
       EVAL_MEMORY,     /* at: its address in the target */
       EVAL_REGISTER,   /* at: 0 to 15, the general register that holds it (its low bytes when it is smaller) */
       EVAL_CONSTANT,   /* at: its offset in the module's location constants */
       EVAL_VALUE };    /* at: the value itself (an integer with its sign carried to 64 bits; a float or double as its bits) */
typedef struct { int kind; EvalType type; uint64_t at; } EvalValue;

/* called: the frame is not the innermost: it waits in a call, and its instruction is the one that ends at its
   return address. (RAD looks names up at the return address itself, and adds the scope one byte before: after a
   call that ends or begins a block it finds the other block's variable of the same name. gdb and lldb do as here.)
   error: where to put why the kind is EVAL_NONE (null when it is not): a text that lives for ever. */
EvalValue eval(const Engine *e, const TargetRegs *frame, bool called, const char *text, const char **error);
/* How many bytes a value of the type has. */
uint32_t eval_size(EvalType type);
/* The first n bytes of the value: any number of them from memory, up to 8 otherwise. */
bool eval_read(const Engine *e, const TargetRegs *frame, EvalValue v, void *bytes, uint32_t n);
#endif
