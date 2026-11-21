/* Name resolution (KB 03-resolver.md): every name in a T-SQL text, with
   what it refers to.  Every refactor is a function over this list; the
   same code runs on the buffer and on the snapshot's module texts. */
#ifndef RESOLVE_H
#define RESOLVE_H
#include "snap.h"

/* what a reference is */
enum {
	RF_OBJECT,      /* a table, view or function name in FROM (span: the whole multi-part name) */
	RF_ALIAS,       /* an alias defined in FROM, or a derived table's name */
	RF_CTE_DEF,     /* a CTE's name where WITH defines it; a temp table's or table variable's where it is made */
	RF_CTE,         /* a CTE, temp table or table variable used */
	RF_QUAL,        /* the x of x.c, or s.t of s.t.c: names a source */
	RF_COLUMN,      /* a column */
	RF_STAR,        /* * or x.* in a select list */
	RF_OUT_DEF,     /* an output alias: c AS name, name = c */
	RF_OUT,         /* ORDER BY naming an output column */
};
/* how it resolved: RS_OK, or the SQL Server error it would give */
enum { RS_OK, RS_UNSURE, RS_NOCOL = 207, RS_AMBIG = 209, RS_NOOBJ = 208, RS_SAMENAME = 1013, RS_SAMEALIAS = 1011, RS_UNBOUND = 4104 };

typedef struct {
	uint32_t s, e;       /* bytes of the name in the text */
	uint8_t kind;
	uint16_t status;
	int32_t src;         /* the source it names or belongs to, or -1 */
	int32_t col;         /* RF_COLUMN: column index in the source; RF_OUT: output index */
	uint8_t wr;          /* RF_COLUMN: written (SET, INSERT's column list, OUTPUT INTO's) */
} Ref;

enum { SRC_TABLE, SRC_DERIVED, SRC_CTE, SRC_UNKNOWN };
typedef struct {
	uint8_t kind;
	int32_t blk;         /* the block whose FROM has it */
	int32_t obj;         /* SRC_TABLE: index into the snapshot's objects */
	int32_t inner;       /* SRC_DERIVED, SRC_CTE: the block giving its columns */
	uint32_t ns, ne;     /* exposed name: the alias, else the last name part (0, 0: none) */
	uint32_t s, e;       /* the whole source, alias included */
	uint32_t cols, ncols;/* a column list (t(a, b), OPENJSON WITH): Res.names[cols .. cols+ncols) */
	uint8_t list;        /* it has one (else the columns are inner's outputs) */
	uint8_t hidden;      /* replaced by a PIVOT/UNPIVOT after it */
	uint8_t aliased;     /* its exposed name is an alias (2: inserted or deleted, no reference to the table) */
	uint16_t grp;        /* which comma-separated item of the FROM list */
} Src;

/* an output column of a block, or a name in a column list */
typedef struct {
	uint32_t ns, ne;     /* its name in the text (0, 0: none) */
	const char *cat;     /* or its catalog name (a star's column), NULL */
	int32_t src, col;    /* a plain column: where it comes from, else -1 */
} Out;

typedef struct {
	int32_t parent;      /* the block whose names it sees, or -1 */
	uint32_t s, e;       /* SELECT to the block's end */
	uint32_t out, nout;  /* Res.out[out .. out+nout) */
} Blk;

typedef struct {
	const Snap *snap;
	const char *text;
	size_t n;
	Ref *r; size_t nr;
	Src *src; size_t nsrc;
	Blk *b; size_t nb;
	Out *out; size_t nout;
	Out *names; size_t nnames;             /* column lists */
	int32_t *top; size_t ntop;             /* each statement's query: its first block */
	void *priv;
} Res;

/* resolves all of text; 0 ok, -1 out of memory */
int  resolve(Res *res, const Snap *snap, const char *text, size_t n);
void resolve_free(Res *res);
/* the columns of source sc; SIZE_MAX when not known (SRC_UNKNOWN, or a
   recursive CTE's own body before its first part) */
size_t src_ncols(const Res *res, size_t sc);
/* column i of source sc: its name (*cat: the snapshot column, or -1) */
Name   src_col(const Res *res, size_t sc, size_t i, long *cat);
/* what column col of source sc finally is, through derived tables and
   CTEs: a snapshot column index (>= 0), or for a computed output of a
   block, -2 - (block << 16 | output); -1 unknown */
long   res_origin(const Res *res, int sc, int col);
/* the name of an output column, NULL s when it has none */
Name   out_name(const Res *res, const Out *o);
#endif
