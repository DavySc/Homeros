/* What the editor asks about a stopped program, and the answers: each the bytes of one message (the knowledge base,
   09 "The engine thread": the editor never calls the engine and gets copies). The engine thread works an ask out
   between two runs (session.h, SESSION_ASK); nothing here is kept from one ask to the next: which rows are open, which
   frame is looked at and what is watched is the editor's to keep and to send. */
#ifndef DBG_ASK_H
#define DBG_ASK_H
#include "view.h"

enum { ASK_STACK,    /* the frames of `thread` from number `first` on, at most `most`. Answer: an AskFrame each. */
       ASK_ROWS,     /* a table at frame `frame` of `thread`: its rows from `first`, at most `most`. A View follows (the open rows), then
                        for `roots` ASK_TEXTS the expressions, each with its 0. Answer: the number of all rows (8 bytes), then an AskRow each. */
       ASK_THREADS,  /* the program's threads. Answer: their ids, 4 bytes each (as many as a message holds). */
       ASK_PLACES }; /* where code is: the text follows, with its 0: `file:line` (the file by the end of its path) or a function's
                        name. Answer: its addresses, 8 bytes each (a line's code can be in several places). */
enum { ASK_TEXTS, ASK_LOCALS, ASK_REGISTERS };   /* a table's roots: what the ask brings, the variables in scope, the registers */
typedef struct { uint32_t kind, thread, frame, roots, most, tag; uint64_t first; } Ask;   /* tag: comes back with the answer (SessionEvent) */

/* name: the function's, cut to fit (empty: none known); module, file: the ends of their paths that fit. */
typedef struct { uint64_t addr; uint32_t line; char name[92], module[32], file[256]; } AskFrame;
typedef struct { ViewRow row; char value[128], type[64]; } AskRow;
enum { ASK_MOST = 64 };   /* frames or rows in one answer, where the room allows them */

/* The answer to the ask of n bytes, written to out (aligned for a uint64_t); its length, which is at most cap (as many whole
   frames, rows or addresses as fit). An ask that makes no sense, or about what is not there, has an empty answer. */
uint32_t ask_answer(const Engine *e, const void *ask, uint32_t n, void *out, uint32_t cap);
#endif
