/* libFuzzer: any reply bytes through the token parser.  Byte 0 picks a split:
   the reply parsed whole must give the same text, rows and state as the
   reply parsed in two pieces (a token restarts from its first byte when
   bytes run out), and a whole reply never leaves the parser waiting (L49). */
#include "../client/tds.c"
#include "../client/net.c"

static int run(Tds *t, const uint8_t *d, size_t n, size_t cut)
{
	memset(t, 0, sizeof *t);
	t->net.fd = -1, t->state = TDS_BUSY, t->pkt = 4096;
	int r = 0;
	if (cut) r = put(&t->msg, d, cut) || parse(t);
	if (!r) r = put(&t->msg, d + cut, n - cut) || (t->eom = 1, parse(t));
	return r;
}

int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n)
{
	if (!n) return 0;
	size_t cut = n > 1 ? d[0] % n : 0;
	d++, n--;
	cut = cut > n ? n : cut;
	Tds a, b;
	int ra = run(&a, d, n, 0), rb = run(&b, d, n, cut);
	if (!ra && a.state == TDS_BUSY) abort();   /* a whole reply left waiting */
	if (ra != rb || (!ra && (a.out.n != b.out.n || (a.out.n && memcmp(a.out.p, b.out.p, a.out.n)) || a.rows != b.rows || a.state != b.state || a.txn != b.txn))) abort();
	tds_close(&a), tds_close(&b);
	return 0;
}
