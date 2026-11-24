/* libFuzzer: any server bytes through package 22's PostgreSQL message
   parser.  Byte 0 picks a cut: the bytes parsed whole must give the same
   text, rows, state and transaction state as the bytes parsed in two
   pieces (a message waits until all of it has arrived). */
#include <stdlib.h>
#include <string.h>
#include "../src/sql/pg.h"

static int run(Pg *p, const uint8_t *d, size_t n, size_t cut)
{
	*p = (Pg){ .net.fd = -1, .kill.fd = -1, .state = PG_BUSY };
	int r = str_add(&p->in, (const char *)d, cut) || pg_parse(p);
	return r || str_add(&p->in, (const char *)d + cut, n - cut) || pg_parse(p);
}

int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n)
{
	if (!n) return 0;
	size_t cut = d[0] % n;
	d++, n--;
	Pg a, b;
	int ra = run(&a, d, n, 0), rb = run(&b, d, n, cut);
	if (ra != rb || (!ra && (a.out.n != b.out.n || (a.out.n && memcmp(a.out.s, b.out.s, a.out.n)) || a.rows != b.rows || a.state != b.state || a.txn != b.txn))) abort();
	pg_close(&a), pg_close(&b);
	return 0;
}
