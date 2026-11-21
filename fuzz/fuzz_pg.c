/* libFuzzer: any server bytes through the PostgreSQL message parser.  Byte 0
   picks a split: whole or in two pieces, the same text, rows and state. */
#include "../client/pg.c"
#include "../client/net.c"

static int run(Pg *p, const uint8_t *d, size_t n, size_t cut)
{
	memset(p, 0, sizeof *p);
	p->net.fd = p->kill.fd = -1, p->state = PG_BUSY;
	int r = buf_put(&p->in, d, cut) || parse(p);
	return r || buf_put(&p->in, d + cut, n - cut) || parse(p);
}

int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n)
{
	if (!n) return 0;
	size_t cut = n > 1 ? d[0] % n : 0;
	d++, n--;
	cut = cut > n ? n : cut;
	Pg a, b;
	int ra = run(&a, d, n, 0), rb = run(&b, d, n, cut);
	if (ra != rb || (!ra && (a.out.n != b.out.n || (a.out.n && memcmp(a.out.p, b.out.p, a.out.n)) || a.rows != b.rows || a.state != b.state || a.txn != b.txn))) abort();
	pg_close(&a), pg_close(&b);
	return 0;
}
