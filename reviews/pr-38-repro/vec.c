/* PR 38 review: the editor's sha256 (static in undo.c, reached by including it)
   against FIPS 180-4 / NIST example digests and against OpenSSL on the same bytes.
   Build with -DUNDO='"undo_new.c"' or undo_old.c. */
#include UNDO
#include <openssl/sha.h>
static unsigned long long r = 88172645463325252ull;
static unsigned long long nx(void) { r ^= r << 13; r ^= r >> 7; r ^= r << 17; return r; }
static long ncmp;
static void hex(const unsigned char *d, char *o) { for (int i = 0; i < 32; i++) sprintf(o + 2 * i, "%02x", d[i]); }
/* frag: build the rope by inserts at random places, so text_read crosses pieces */
static void one(const unsigned char *s, size_t n, const char *want, int frag)
{
	Text *t;
	if (!frag) t = text_new((const char *)s, n);
	else {
		t = text_new("", 0);
		/* pieces of 1..97 bytes, last first, each inserted at the front */
		size_t left = n;
		while (left) {
			size_t k = 1 + nx() % 97; if (k > left) k = left;
			left -= k;
			if (text_insert(t, 0, (const char *)s + left, k)) abort();
		}
	}
	unsigned char got[32], ref[32]; char a[65], b[65];
	sha256(t, got);
	SHA256(s, n, ref);
	hex(got, a); hex(ref, b);
	if (strcmp(a, b)) { printf("DIFF from OpenSSL at len %zu: %s vs %s\n", n, a, b); exit(1); }
	if (want && strcmp(a, want)) { printf("DIFF from NIST vector at len %zu: %s vs %s\n", n, a, want); exit(1); }
	text_free(t); ncmp++;
}
int main(int argc, char **argv)
{
	long nrand = argc > 1 ? atol(argv[1]) : 2000;
	/* NIST FIPS 180-4 examples (csrc.nist.gov SHA256.pdf, SHA2_Additional.pdf) and SHAVS */
	one((const unsigned char *)"", 0, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", 0);
	one((const unsigned char *)"abc", 3, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", 0);
	one((const unsigned char *)"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56,
	    "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1", 0);
	one((const unsigned char *)"abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu", 112,
	    "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1", 0);
	size_t M = 1000000; unsigned char *m = malloc(M); memset(m, 'a', M);
	one(m, M, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0", 0);
	/* SHA2_Additional: 1 byte 0xbd; 4 bytes c98c8e55; 55, 56, 57, 64 zero bytes; 1000 zero bytes */
	one((const unsigned char *)"\xbd", 1, "68325720aabd7c82f30f554b313d0570c95accbb7dc4b5aae11204c08ffe732b", 0);
	one((const unsigned char *)"\xc9\x8c\x8e\x55", 4, "7abc22c0ae5af26ce93dbb94433a0e0b2e119d014f8e7f65bd56c61ccccd9504", 0);
	memset(m, 0, 1000);
	one(m, 55, "02779466cdec163811d078815c633f21901413081449002f24aa3e80f0b88ef7", 0);
	one(m, 56, "d4817aa5497628e7c77e6b606107042bbba3130888c5f47a375e6179be789fbb", 0);
	one(m, 57, "65a16cb7861335d5ace3c60718b5052e44660726da4cd13bb745381b235a1785", 0);
	one(m, 64, "f5a5fd42d16a20302798ef6ed309979b43003d2320d9f0e8ea9831a92759fb4b", 0);
	one(m, 1000, "541b3e9daa09b20bf85fa273e5cbd3e80185aa4ec298e765db87742b70138a53", 0);
	/* every length 0..2100 with random bytes, whole and in pieces: all padding cases, many blocks */
	size_t B = 4u << 20; unsigned char *b = malloc(B);
	for (size_t n = 0; n <= 2100; n++) {
		for (size_t i = 0; i < n; i++) b[i] = (unsigned char)nx();
		one(b, n, 0, 0); one(b, n, 0, 1);
	}
	/* random lengths up to 4 MiB, random bytes */
	for (long k = 0; k < nrand; k++) {
		size_t n = k % 4 == 0 ? nx() % B : nx() % 70000;
		for (size_t i = 0; i < n; i++) b[i] = (unsigned char)nx();
		one(b, n, 0, k % 8 == 1);
	}
	free(m); free(b);
	printf("ok: %ld inputs equal to OpenSSL %s, 12 of them also to the NIST digest\n", ncmp, OPENSSL_VERSION_TEXT);
	return 0;
}
