/* What a person sees of values (the knowledge base, 05 "The watch tree" and 06 "The watch table"): a value and
   a type as text, and the rows of a list of expressions of which some are opened into their members, elements
   or what they point at. A row is an expression's text, worked out whole by eval at each look; which rows are
   open is kept by where they are in the tree (RAD's EV_Key), so it holds while the program runs on. */
#ifndef DBG_VIEW_H
#define DBG_VIEW_H
#include "eval.h"

enum { VIEW_TEXT = 256,   /* a row's expression with its end; a row deeper than fits has an empty text */
       VIEW_OPEN = 256 }; /* rows that can be open at once */

/* The expression's value as a person reads it, written as RAD writes it: 'c' (99), GREEN (5), 2.0, an address with the text at it,
   [1, 2, 3], a record as its members' values in braces; why it has none when it has none. */
void view_value(const Engine *e, const EvalFrame *frame, const char *expression, char *text, uint32_t n);
/* The type as C writes it (an array of arrays with its counts in C's order; a pointer to an array as `int [3] *`). */
void view_type(EvalType type, char *text, uint32_t n);

/* A row: the child `child` of the row with key `parent` (a root: parent 0, child from its text). label: where in text
   the part begins that this row adds to its parent's. */
typedef struct { uint64_t parent, child; uint32_t depth, label; char text[VIEW_TEXT]; } ViewRow;
typedef struct { struct { uint64_t parent, child; } open[VIEW_OPEN]; uint32_t count; } View;

/* Opens the row, or closes it when it is open (what is open below it stays so for the next time). False: no room. */
bool view_toggle(View *view, const ViewRow *row);
/* The rows from number `first` on, at most `most` of them, of the roots with all that is open below them; how many were
   written to *shown. Gives the number of all rows. Rows outside the window cost nothing unless they are open. */
uint64_t view_rows(const View *view, const Engine *e, const EvalFrame *frame, const char *const *roots, uint32_t root_count,
                   uint64_t first, ViewRow *rows, uint32_t most, uint32_t *shown);

/* A frame of a thread's stack. An inlined call is a frame of its own with the registers of the function it is in, which comes after it
   (RAD's d_call_stack_from_unwind). at: what eval and the tables take. name: the function's, empty when the debug information has
   none; line: where it stands in its own source, file 0 when unknown. */
typedef struct { EvalFrame at; const EngineModule *module; RdiSpan name; RdiLine line; } ViewFrame;
/* The frames from number `first` on (0: the innermost, where the thread has these registers) outward: at most `most`; how many.
   The stack is walked as far as the last frame given and no further. */
uint32_t view_stack(const Engine *e, TargetRegs regs, uint32_t first, ViewFrame *frames, uint32_t most);
#endif
