/* The catalog snapshot (KB 02-catalog.md): what the server printed, one
   fact per line, loaded into arrays with lookup by name under the
   dialect's rules.  The editor never talks to the server itself. */
#ifndef SNAP_H
#define SNAP_H
#include <stddef.h>
#include <stdint.h>

enum { D_TSQL, D_PG, D_DB2 };

typedef struct {
	const char *schema, *name, *type;   /* type: U V IF TF FN P SN TT (T-SQL's) */
	const char *text;                   /* module text, NULL for none */
	int64_t id;
	uint32_t col, ncol;                 /* its columns: c[col .. col+ncol) */
} SnapObj;
typedef struct {
	const char *name, *type;
	uint32_t obj;                       /* index into o */
	int32_t id, maxlen;                 /* id: the server's column number (gaps after a drop) */
	uint8_t nullable;
} SnapCol;
typedef struct { uint32_t from, from_col, to, to_col; } SnapFk;   /* indexes into o and into c */

typedef struct {
	int dialect, ci;                    /* ci: T-SQL under a _CI_ collation (D record) */
	const char *dflt;                   /* the session's default schema (D record), or NULL */
	char *mem;
	SnapObj *o; size_t no;
	SnapCol *c; size_t nc;
	SnapFk *k;  size_t nk;
	uint32_t *h; size_t nh;             /* names: o and c, open addressing */
} Snap;

/* NULL out of memory.  Lines that are not records are skipped; a record
   that names an unknown object is dropped (counted in *bad when not NULL). */
Snap *snap_load(const char *s, size_t n, int dialect, size_t *bad);
void  snap_free(Snap *s);
/* An identifier as written: quoted ([x], "x") names compare exactly in
   PostgreSQL and Db2; unquoted ones fold first (lower, UPPER).  T-SQL
   compares by the database collation from the D record: ignoring ASCII
   case under a _CI_ one, else exactly (non-ASCII case and accent rules
   are not modelled: KB 03 rule 11). */
typedef struct { const char *s; size_t n; int quoted; } Name;
/* index of object schema.name, or -1; schema.s NULL: the default schema's
   first, else the only one of that name (-2 when several match) */
long  snap_object(const Snap *s, Name schema, Name name);
/* two names the same under the snapshot's rules (a catalog spelling is
   quoted: exact) */
int   snap_eq(const Snap *s, Name a, Name b);
/* index into c of object o's column, or -1 */
long  snap_column(const Snap *s, size_t o, Name col);
/* Name of a token [s, s+n): strips [..], ".." or '..' (a column alias may
   be a string) and undoubles ]], "" or '' into buf (size n), sets quoted */
Name  snap_name(const char *s, size_t n, char *buf);
#endif
