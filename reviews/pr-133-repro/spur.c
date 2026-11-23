#include <stdio.h>
#include "src/sql/pg.h"
int main(void) { Pg g; pg_open(&g, "127.0.0.1", 5432, "postgres", "x", "postgres", 1); printf("pg_io(readable=1) before any byte: %d state %d\n", pg_io(&g, 1), g.state); pg_close(&g); return 0; }
