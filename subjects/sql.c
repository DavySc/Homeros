/* A real C program on the bench: SQLite, the whole library in this one unit (subjects/sqlite/, fetched once by the
   recorder's workflow), taken through opening a database, three statements and a query that calls back.
   What the small subjects cannot ask: 9 MB of one author's C, its macros, function pointers and switch tables,
   as the compiler lays them out with and without optimising, and with /JMC. */
#include "sqlite3.c"

static int rows;

static int row(void *sum, int n, char **text, char **name)
{
  (void)name;
  for (int i = 0; i < n; i += 1) *(int *)sum += text[i] ? (int)strlen(text[i]) : 0;
  rows += 1;
  return 0;
}

int main(void)
{
  sqlite3 *db;
  int sum = 0;
  if (sqlite3_open(":memory:", &db)) return 1;
  sqlite3_exec(db, "create table t(a integer primary key, b text); insert into t(b) values('one'),('two'),('three');", 0, 0, 0);
  sqlite3_exec(db, "select a, upper(b) from t where a > 1 order by b desc", row, &sum, 0);
  sqlite3_close(db);
  return (sum + rows) & 0;
}
