/* The undo file and the size cap (src/undo).  A tree written and read back
   behaves as the original under any further moves; a file for another text,
   a truncated file or a damaged one is refused or, if a damaged byte leaves
   it consistent, every move still works; the hash is SHA-256 (FIPS 180-4
   vectors); a newhead that is not the newest change of its line is refused;
   'undolevels' 1000 keeps 1,001 changes, as Neovim; counts at the ends of
   long's range do not overflow.  The clock is
   this file's time(), stepped between ops, so both copies of a tree see the
   same times and time moves are exercised. */
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "check.h"
#include "undo.h"

enum { CAP = 1 << 15 };
static char path[] = "/tmp/homeros-undo-XXXXXX";
static time_t now = 1700000000;
time_t time(time_t *t) { if (t) *t = now; return now; }

static size_t all(const Text *t, char *b) { return text_read(t, 0, b, CAP); }

static void edit(Text *t, Undo *u)
{
	size_t len = text_len(t), pos = rnd((unsigned)len + 1), nd = rnd((unsigned)(len - pos < 3 ? len - pos : 3) + 1);
	char del[4], ins[] = "ab\ncd";
	size_t ni = len > CAP - 8 ? 0 : rnd(6);
	text_read(t, pos, del, nd);
	CHECK(!text_delete(t, pos, nd) && !text_insert(t, pos, ins, ni));
	CHECK(!undo_record(u, pos, del, nd, ins, ni, (UndoCursor){ rnd(9), rnd(9), rnd(2) }, rnd(2)));
}

/* one random op; returns the move's result */
static int op(Text *t, Undo *u, unsigned k)
{
	UndoWhere w;
	int r = 0;
	switch (k % 9) {
	case 0: case 1: edit(t, u); if (rnd(3)) undo_close(u); break;
	case 2: r = undo_undo(u, t, &w); break;
	case 3: r = undo_redo(u, t, &w); break;
	case 4: undo_saved(u); break;
	case 5: r = undo_time(u, (long)rnd(9) - 4, UNDO_SEQ); break;
	case 6: r = undo_time(u, (long)rnd(5) - 2, UNDO_FILE); break;
	case 7: r = undo_time(u, (long)rnd((unsigned)undo_seq(u) + 3), UNDO_ABS); break;
	default: r = undo_time(u, (long)rnd(5) - 2, UNDO_SEC); break;
	}
	CHECK(r >= 0 || (r == -1 && k % 9 == 7));   /* -1 only for :undo N (E830) */
	if (k % 9 >= 5 && r > 0) {
		int s;
		while ((s = undo_step(u, t, &w)) > 0) {}
		CHECK(s == 0);
	}
	return r;
}

static void same(const Text *a, const Undo *ua, const Text *b, const Undo *ub)
{
	static char x[CAP], y[CAP];
	size_t n = all(a, x);
	CHECK(n == all(b, y) && !memcmp(x, y, n) && undo_seq(ua) == undo_seq(ub));
}

static void sha(const char *s, size_t n, const char *hex)
{
	Text *t = text_new(s, n);
	Undo *u = undo_new();
	unsigned char b[40];
	CHECK(t && u && !undo_write(u, t, path));
	FILE *f = fopen(path, "rb");
	CHECK(f && fread(b, 1, 40, f) == 40);
	fclose(f);
	for (int i = 0; i < 32; i++) {
		unsigned v;
		CHECK(sscanf(hex + 2 * i, "%2x", &v) == 1 && b[8 + i] == v);
	}
	undo_free(u);
	text_free(t);
}

int main(void)
{
	int fd = mkstemp(path);
	CHECK(fd >= 0);
	close(fd);
	static char big[1280];
	for (int i = 0; i < 1280; i++) big[i] = (char)i;
	sha("", 0, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
	sha("abc", 3, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
	sha("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", 55, "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318");
	sha("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", 56, "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a");
	sha("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", 64, "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb");
	sha(big, 1280, "d414b085826eb06778483ba35564dc849e643359f69ed9747878ba6e54985bed");

	/* round trip: same text and state number after every one of 200 more ops */
	static char buf[CAP];
	int damaged = 0, kept = 0;
	for (int round = 0; round < 200; round++) {
		Text *t = text_new("hello\nworld\n", 12);
		Undo *u = undo_new();
		CHECK(t && u);
		for (int i = (int)rnd(120); i > 0; i--) {
			op(t, u, rnd(9));
			now += rnd(3);
		}
		undo_close(u);   /* a write comes from normal mode */
		CHECK(!undo_write(u, t, path));
		Text *t2 = text_new(buf, all(t, buf));
		Undo *u2 = undo_read(t2, path);
		CHECK(t2 && u2);
		for (int i = 0; i < 200; i++) {
			unsigned k = rnd(9);
			now += rnd(3);
			unsigned long long s = rng;
			int r = op(t, u, k);
			rng = s;
			CHECK(op(t2, u2, k) == r);
			same(t, u, t2, u2);
		}
		undo_free(u2);
		text_free(t2);
		/* a file for another text is refused */
		CHECK(!undo_write(u, t, path));
		CHECK(!text_insert(t, 0, "x", 1) && !undo_read(t, path));
		CHECK(!text_delete(t, 0, 1));
		/* truncated anywhere: refused; one byte changed: refused, or every
		   move still applies (op() CHECKs that no move fails) */
		FILE *f = fopen(path, "rb");
		static unsigned char file[1 << 20];
		size_t n = fread(file, 1, sizeof file, f);
		fclose(f);
		for (int i = 0; i < 20; i++) {
			size_t cut = rnd((unsigned)n), at = rnd((unsigned)n);
			f = fopen(path, "wb");
			CHECK(fwrite(file, 1, cut, f) == cut);
			fclose(f);
			CHECK(!undo_read(t, path));
			file[at] ^= (unsigned char)(1 + rnd(255));
			f = fopen(path, "wb");
			CHECK(fwrite(file, 1, n, f) == n);
			fclose(f);
			Undo *d = undo_read(t, path);
			damaged++;
			if (d) {
				kept++;
				Text *t3 = text_new(buf, all(t, buf));
				for (int j = 0; j < 100; j++) op(t3, d, rnd(9));
				text_free(t3);
				undo_free(d);
			}
			file[at] = 0;   /* restore from a fresh write next time */
			CHECK(!undo_write(u, t, path));
			f = fopen(path, "rb");
			CHECK(fread(file, 1, n, f) == n);
			fclose(f);
		}
		undo_free(u);
		text_free(t);
	}
	{
		Text *t = text_new("", 0);
		CHECK(t && !undo_read(t, "/nonexistent/homeros-undo"));
		text_free(t);
	}

	/* newhead changed to an older change of its line (review of package 2,
	   finding 3): the edits still apply, as all three type "x" at 0, but
	   open_group() would cut the newer changes off, so it is refused */
	{
		Text *t = text_new("", 0);
		Undo *u = undo_new();
		for (int i = 0; i < 3; i++) {
			CHECK(!text_insert(t, 0, "x", 1) && !undo_record(u, 0, "", 0, "x", 1, (UndoCursor){ 0, 0, 0 }, 0));
			undo_close(u);
		}
		CHECK(!undo_write(u, t, path));
		FILE *f = fopen(path, "r+b");
		CHECK(f && !fseek(f, 96, SEEK_SET) && fgetc(f) == 3);   /* newhead: change 3 */
		CHECK(!fseek(f, 96, SEEK_SET) && fputc(1, f) == 1 && !fclose(f));
		CHECK(!undo_read(t, path));
		undo_free(u);
		text_free(t);
	}

	/* the cap: 2,500 changes in a line keep the newest 1,001 (u_savecommon
	   frees down to 'undolevels' before adding), numbered as before */
	{
		Text *t = text_new("", 0);
		Undo *u = undo_new();
		UndoWhere w;
		for (int i = 0; i < 2500; i++) {
			CHECK(!text_insert(t, 0, "x", 1) && !undo_record(u, 0, "", 0, "x", 1, (UndoCursor){ 0, 0, 0 }, 0));
			undo_close(u);
		}
		int n = 0;
		while (undo_undo(u, t, &w) == 1) n++;
		CHECK(n == 1001 && text_len(t) == 1499 && undo_seq(u) == 0);
		CHECK(undo_time(u, 1499, UNDO_ABS) == -1 && undo_time(u, 1500, UNDO_ABS) == 1);
		while (undo_step(u, t, &w) == 1) {}
		CHECK(undo_seq(u) == 1500 && text_len(t) == 1500);
		undo_free(u);
		text_free(t);
	}
	/* counts at the ends of long's range (":later 9223372036854775807"):
	   no overflow (UBSan), and they go to the newest or oldest state */
	{
		Text *t = text_new("", 0);
		Undo *u = undo_new();
		UndoWhere w;
		CHECK(!text_insert(t, 0, "x", 1) && !undo_record(u, 0, "", 0, "x", 1, (UndoCursor){ 0, 0, 0 }, 0));
		undo_close(u);
		undo_saved(u);
		for (int unit = UNDO_SEQ; unit <= UNDO_FILE; unit++)
			for (int i = 0; i < 4; i++) {
				long step = i & 1 ? LONG_MIN + (i >> 1) : LONG_MAX - (i >> 1);
				CHECK(undo_time(u, step, unit) >= 0);
				while (undo_step(u, t, &w) > 0) {}
				CHECK(undo_seq(u) == (step > 0) && text_len(t) == (step > 0));
			}
		CHECK(undo_time(u, LONG_MAX, UNDO_ABS) == -1 && undo_time(u, LONG_MIN, UNDO_ABS) == -1);
		undo_free(u);
		text_free(t);
	}
	remove(path);
	printf("test_undo_file: SHA-256 vectors; 200 trees read back and moved 200 times in step; "
	       "%d damaged files (%d still consistent, every move applied); the 1,000 cap\n", damaged, kept);
	return 0;
}
