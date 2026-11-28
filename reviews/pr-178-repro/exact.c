/* org_open at every character of each FILE, the text in a heap block of
   exactly its length (no byte after it, none before): under ASan a read
   outside the text stops here, which the driver's and the fuzzer's static
   buffers hide.  Also checks what fuzz_org's check_open checks. */
#include "org.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc, char **argv)
{
	size_t calls = 0;
	for (int i = 1; i < argc; i++) {
		FILE *f = fopen(argv[i], "rb");
		if (!f) return 2;
		static char b[1 << 20];
		size_t n = fread(b, 1, sizeof b, f);
		fclose(f);
		char *s = malloc(n ? n : 1);
		memcpy(s, b, n);
		for (size_t p = 0; p <= n; p++) {
			if (p < n && (s[p] & 0xc0) == 0x80) continue;
			OrgOpen o;
			if (org_open(n ? s : s + 1, n, p, &o)) return 3;
			if (o.kind < ORG_OPEN_GOTO || o.kind > ORG_OPEN_MENU || (o.kind == ORG_OPEN_GOTO && o.pos > n) || !memchr(o.s, 0, sizeof o.s)) { printf("%s at %zu: kind %d pos %zu\n", argv[i], p, o.kind, o.pos); return 1; }
			calls++;
		}
		free(s);
	}
	printf("%zu calls, no read outside the text\n", calls);
	return 0;
}
