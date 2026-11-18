/* Review of PR 53: libFuzzer on file_eol() and file_text_read() against
   their statement: dos exactly when the data has a LF and every LF follows
   a CR; for dos the text is the data without the CR before each LF; and
   reading the text back in chunks of any size (cap from 2) gives the data
   again, never more than cap bytes at a time.  Unix data is left alone.
   clang -g -O1 -fsanitize=fuzzer,address,undefined -Iinclude -Isrc/file \
     reviews/pr-53-repro/fuzz_eol.c src/file/eol.c src/text/text.c */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "file.h"
#include "state.h"

int LLVMFuzzerTestOneInput(const uint8_t *in, size_t len)
{
	if (len < 1) return 0;
	size_t cap = 2 + in[0] % 40, n = len - 1, lf = 0, crlf = 0, m = 0;
	/* d has no spare byte, so a read past the data is seen */
	char *d = malloc(n ? n : 1), *want = malloc(n + 1), *b = malloc(cap), *out = malloc(n + 1);
	/* bytes folded to a small alphabet, so CR and LF are common */
	for (size_t i = 0; i < n; i++) d[i] = "\r\n\r\nab\x1a"[in[i + 1] % 7];
	char *orig = malloc(n + 1);
	memcpy(orig, d, n);
	for (size_t i = 0; i < n; i++) if (d[i] == '\n') { lf++; crlf += i > 0 && d[i - 1] == '\r'; }
	int model = lf > 0 && crlf == lf;
	for (size_t i = 0; i < n; i++) if (!(model && d[i] == '\r' && i + 1 < n && d[i + 1] == '\n')) want[m++] = d[i];
	size_t k = n;
	int dos = file_eol(d, &k);
	if (dos != model || k != m || memcmp(d, want, m)) abort();
	Text *t = text_new(d, k);
	if (!t) abort();
	size_t pos = 0, o = 0;
	while (pos < k) {
		memset(b, 'Q', cap);
		size_t r = file_text_read(t, &pos, k, dos, b, cap);
		if (r == 0 || r > cap || o + r > n) abort();
		memcpy(out + o, b, r);
		o += r;
	}
	if (o != n || memcmp(out, orig, n)) abort();
	text_free(t);
	free(d); free(want); free(b); free(out); free(orig);
	return 0;
}
