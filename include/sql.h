/* SQL buffers (package 22, src/sql/): SSMS's query window over the
   editor.  A .sql buffer gets its connection from the connections file
   ($XDG_CONFIG_HOME/homeros/sql; src/sql/README.md): the longest "dir"
   line whose folder holds the file names it, and :Connect names another
   (Davy, 2026-11-22).  Results go, as SSMS's grid (columns as wide as the
   names and the first 500 rows, capped at 40 cells), to a buffer in a
   split below the query (Davy, 2026-11-23), and the messages after them,
   as SSMS's Messages tab; SPC r opens and closes it. */
#ifndef SQL_H
#define SQL_H
struct Editor;
struct Buf;

/* F5 runs the selection or, with none, the whole buffer (SSMS; GO splits
   SQL Server scripts as sqlcmd does); Alt+Break, or Ctrl-C while it
   runs, cancels the run.  Only in a .sql buffer or its results: 1
   taken, 0 not. */
int  sql_key(struct Editor *e, int key);
/* SPC r: the current SQL buffer's results split opens or closes.  0 ok,
   -1 msg set. */
int  sql_results(struct Editor *e);
/* :Connect name: the current buffer's connection is name's from the
   connections file; no name shows which one it has.  0 ok, -1 msg set. */
int  sql_connect(struct Editor *e, const char *name);
/* :Disconnect: the current SQL buffer's session ends (the server drops its
   temp tables and rolls back an open transaction, as when an SSMS query
   window closes); the next F5 logs in again.  0 ok, -1 msg set. */
int  sql_disconnect(struct Editor *e);
/* The columns of the tables and views on b's connection (SSMS's
   IntelliSense cache), for completion: fn gets each, in schema, table and
   column order.  Read when a session on b's connection logs in (its
   first F5: SSMS's IntelliSense needs a connected window), on a
   connection of its own, in the background, and again at a later login
   if that failed: until then, or with no connection, 0 and no message.
   fn NULL drops it and reads it again (at once if b is logged in, else at
   its login: SSMS's Ctrl+Shift+R).  Returns how many fn got. */
int  sql_catalog(struct Editor *e, const struct Buf *b, void (*fn)(void *ctx, const char *schema, const char *table, const char *column), void *ctx);
#endif
