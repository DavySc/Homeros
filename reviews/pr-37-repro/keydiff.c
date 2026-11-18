/* key() on every byte string of length 1 to 6 over an alphabet of the bytes
   that matter here, with and without force: prints one line per call, so
   two builds (main's tty.c, the PR's, a variant) can be diffed.
   build: gcc -Iinclude -DTTY='"path/to/tty.c"' keydiff.c grid.c ... (see run.sh) */
#include TTY
#include <stdio.h>
int main(void)
{
	static const unsigned char a[] = { 27, '[', '3', '~', ';', '5', 'A', '2', 'O', 'x', 0xC3 };
	enum { N = sizeof a };
	unsigned char s[8];
	for (int len = 1; len <= 6; len++) {
		long total = 1;
		for (int i = 0; i < len; i++) total *= N;
		for (long v = 0; v < total; v++) {
			long w = v;
			for (int i = 0; i < len; i++) { s[i] = a[w % N]; w /= N; }
			s[len] = 0xEE;   /* a wrong read past the end shows under ASan or as a changed line */
			for (int force = 0; force < 2; force++) {
				int k = -2, r = key(s, len, force, &k);
				for (int i = 0; i < len; i++) printf("%02x", s[i]);
				printf(" %d %d %d\n", force, r, k);
			}
		}
	}
	return 0;
}
