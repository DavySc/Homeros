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

/* A frame of a thread's stack: its registers. called: it is not the innermost: it waits in a call, and its instruction is the one that
   ends at its return address. (RAD looks names up at the return address itself, and adds the scope one byte before: after a call that ends
   or begins a block it finds the other block's variable of the same name. gdb and lldb do as here.) inlined: so many inlined calls around
   its instruction are frames of their own inside it (view_stack); their variables are not its own. */
typedef struct { TargetRegs regs; bool called; uint32_t inlined; } EvalFrame;
/* error: where to put why the kind is EVAL_NONE (null when it is not): a text that lives for ever. */
EvalValue eval(const Engine *e, const EvalFrame *frame, const char *text, const char **error);
enum { EVAL_NOTHING, EVAL_INT, EVAL_REAL, EVAL_ADDRESS, EVAL_REFERENCE, EVAL_ROW, EVAL_RECORD };
/* What a type is once its names and modifiers are taken off. to: what an address or a reference leads to, a row's element,
   a record's members (idx: its RdiUdt), a bit field's whole number. count: not 0 for a bit field: so many bits from bit `first`.
   names: not 0 for an enum: the RdiUdt of its names. */
typedef struct { int form; uint32_t size; bool sign, truth; uint32_t first, count, names; EvalType to; } EvalShape;
EvalShape eval_shape(EvalType type);
/* A record's members: RdiMember, of kind 1 those that are data. */
RdiSpan eval_members(EvalShape record);
/* What C reads at a place: a number or an address, its sign carried to 64 bits; of an array, the address of its first
   element (a bit field of fewer bits than an int as an int). */
EvalValue eval_load(const Engine *e, const EvalFrame *frame, EvalValue v, const char **error);
/* The names of the variables around the frame's instruction, the innermost first, each once, as far out as its function
   (an inlined call is one); how many (up to most). */
uint32_t eval_locals(const Engine *e, const EvalFrame *frame, RdiSpan *names, uint32_t most);
#endif
