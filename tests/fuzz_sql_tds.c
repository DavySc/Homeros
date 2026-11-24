/* libFuzzer: any reply bytes through package 22's TDS token parser.  Byte 0
   picks a cut: the reply parsed whole must give the same text, rows and
   state as the reply parsed in two pieces (a token restarts from its first
   byte when the bytes run out), and a whole reply never leaves the parser
   waiting (KB lesson L49), nor agrees to a packet size outside [MS-TDS]'s. */
#include <stdlib.h>
#include <string.h>
#include "../src/sql/tds.h"

static int run(Tds *t, const uint8_t *d, size_t n, size_t cut)
{
	*t = (Tds){ .net.fd = -1, .state = TDS_BUSY, .pkt = 4096 };
	int r = cut ? str_add(&t->msg, (const char *)d, cut) || tds_parse(t) : 0;
	return r || str_add(&t->msg, (const char *)d + cut, n - cut) || (t->eom = 1, tds_parse(t));
}

int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n)
{
	if (!n) return 0;
	size_t cut = d[0] % n;
	d++, n--;
	Tds a, b;
	int ra = run(&a, d, n, 0), rb = run(&b, d, n, cut);
	if (!ra && a.state == TDS_BUSY) abort();   /* a whole reply left waiting */
	if (a.pkt < 512 || a.pkt > 32767) abort();   /* send_msg's buffer holds one packet */
	if (ra != rb || (!ra && (a.out.n != b.out.n || (a.out.n && memcmp(a.out.s, b.out.s, a.out.n)) || a.rows != b.rows || a.state != b.state || a.txn != b.txn || a.pkt != b.pkt))) abort();
	tds_close(&a), tds_close(&b);
	return 0;
}
