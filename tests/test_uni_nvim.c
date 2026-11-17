/* src/uni against Neovim itself and against Unicode's own test file.
   nvim --headless reaches its own utfc_ptr2len(), utf_ptr2cells() and
   utf8proc_get_property() through LuaJIT's FFI, so the oracle is the code
   the editor copies, not a reading of it.  Three parts:
   1. every code point below 2^21: the tables' class and InCB against
      utf8proc (the facts uni.c admits), and uni_width alone and before
      U+FE0F against utf_ptr2cells (exhaustive);
   2. 20,000 random lines: cluster starts and widths against utfc_ptr2len
      and utf_ptr2cells, and uni_gprev from every byte against the starts;
   3. GraphemeBreakTest.txt (Unicode 18): every case, with Neovim's one
      exception (ASCII never joins) applied to the expected breaks. */
#include <string.h>
#include "check.h"
#include "uni.h"
#include "../src/uni/tab.h"

#define TOP 0x200000  /* past the largest code point four bytes can hold */
#define LINES 20000

static const char lua[] =
	"local ffi = require('ffi')\n"
	"ffi.cdef[[typedef struct { int16_t a, b, c, d; uint16_t e, f, g, h, i;\n"
	"  uint16_t j:10, k:5, l:1; unsigned m:1, n:1, o:1, p:1, w:2, amb:1, pad:1, bc:6, icb:2; } P;\n"
	"  const P *utf8proc_get_property(int32_t c); int utfc_ptr2len(const char *p); int utf_ptr2cells(const char *p);]]\n"
	"local C, ch, fl = ffi.C, string.char, math.floor\n"
	"local function enc(c)\n"  /* ASCII as its overlong 2-byte form: what uni.c can meet there */
	"  if c < 0x800 then return ch(0xC0 + fl(c / 64), 0x80 + c % 64) end\n"
	"  if c < 0x10000 then return ch(0xE0 + fl(c / 4096), 0x80 + fl(c / 64) % 64, 0x80 + c % 64) end\n"
	"  return ch(0xF0 + fl(c / 262144), 0x80 + fl(c / 4096) % 64, 0x80 + fl(c / 64) % 64, 0x80 + c % 64)\n"
	"end\n"
	"local f = io.open(arg[1] .. '.props', 'wb')\n"
	"for b = 0, 0x1FFFFF, 0x10000 do\n"
	"  local t = {}\n"
	"  for c = b, b + 0xFFFF do\n"
	"    local p, s = C.utf8proc_get_property(c), enc(c)\n"
	"    t[#t + 1] = ch(p.bc, p.icb, C.utf_ptr2cells(s), C.utf_ptr2cells(s .. '\\239\\184\\143'))\n"
	"  end\n"
	"  f:write(table.concat(t))\n"
	"end\n"
	"f:close()\n"
	"f = io.open(arg[1] .. '.segs', 'w')\n"
	"for l in io.lines(arg[1] .. '.lines') do\n"
	"  local p, i, t = ffi.cast('const char *', l), 0, {}\n"
	"  while i < #l do\n"
	"    t[#t + 1] = i .. ' ' .. (l:byte(i + 1) < 0x80 and 1 or C.utf_ptr2cells(p + i))\n"
	"    i = i + C.utfc_ptr2len(p + i)\n"
	"  end\n"
	"  f:write(table.concat(t, ' '), '\\n')\n"
	"end\n"
	"f:close()\n";

static size_t enc(unsigned c, unsigned char *s)
{
	if (c < 0x800) { s[0] = (unsigned char)(0xC0 + c / 64); s[1] = (unsigned char)(0x80 + c % 64); return 2; }
	if (c < 0x10000) {
		s[0] = (unsigned char)(0xE0 + c / 4096); s[1] = (unsigned char)(0x80 + c / 64 % 64);
		s[2] = (unsigned char)(0x80 + c % 64); return 3;
	}
	s[0] = (unsigned char)(0xF0 + c / 262144); s[1] = (unsigned char)(0x80 + c / 4096 % 64);
	s[2] = (unsigned char)(0x80 + c / 64 % 64); s[3] = (unsigned char)(0x80 + c % 64); return 4;
}

static unsigned prop(unsigned c) { return c < 0x110000 ? uni_t2[uni_t1[c / UNI_B]][c % UNI_B] : 0; }

/* the class codes of gen.py, by utf8proc's boundclass (-1: never in the data) */
static const int cls[21] = { -1, 0, 1, 1, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, -1, -1, -1, -1, 12, -1 };

static unsigned *bucket[64];
static size_t nbucket[64];

/* one random line: cluster-relevant code points of every class, ASCII,
   stray and truncated bytes, overlong and out-of-range sequences */
static size_t line(unsigned char *s)
{
	static const unsigned special[] = { 0xFE0F, 0x200D, 0x644, 0x622, 0x627, 0x1F600, 0x1F1F3, 0x1F1F1,
		0x2764, 0x915, 0x94D, 0x937, 0x600, 0x85, 0x1100, 0x1161, 0x11A8, 0xAC00, 0xAC01, 0x301,
		0x903, 0xA9, 0x1F3FB, 0x1F468, 0x1F469, 0x4E2D };
	static const char *odd[] = { "\xc1\x81", "\xc0\x80", "\xe0\x80\x81", "\xf4\x90\x80\x80", "\xf7\xbf\xbf\xbf",
		"\xed\xa0\x80" };
	size_t n = 0;
	for (unsigned k = 1 + rnd(12); k--;) {
		unsigned x = rnd(100), c;
		if (x < 35) n += enc(special[rnd(sizeof special / sizeof *special)], s + n);
		else if (x < 75) {
			unsigned b;
			do b = rnd(64); while (!nbucket[b]);
			n += enc(bucket[b][rnd((unsigned)nbucket[b])], s + n);
		} else if (x < 85) s[n++] = (unsigned char)"a e1\t~"[rnd(6)];
		else if (x < 92) s[n++] = (unsigned char)(0x80 + rnd(0x80));
		else if (x < 96) { c = 0x80 + rnd(0x110000 - 0x80); n += enc(c, s + n) - rnd(2); }
		else { const char *o = odd[rnd(sizeof odd / sizeof *odd)]; memcpy(s + n, o, strlen(o)); n += strlen(o); }
	}
	return n;
}

static void graphemetest(void)
{
	FILE *f = fopen("src/uni/GraphemeBreakTest.txt", "r");
	CHECK(f);
	char l[4096];
	int cases = 0;
	while (fgets(l, sizeof l, f)) {
		if (l[0] == '#' || l[0] == '\n') continue;
		unsigned char s[256];
		size_t n = 0, at[64];
		int brk[64], k = 0;   /* brk[k]: the file breaks before code point k */
		for (char *p = l; *p && *p != '#';) {
			if (!strncmp(p, "\xc3\xb7", 2)) { brk[k] = 1; p += 2; }        /* ÷ */
			else if (!strncmp(p, "\xc3\x97", 2)) { brk[k] = 0; p += 2; }   /* × */
			else if (*p == ' ' || *p == '\t') p++;
			else {
				unsigned c = (unsigned)strtoul(p, &p, 16);
				at[k] = n;
				if (c < 0x80) { s[n++] = (unsigned char)c; brk[k] = 1; }   /* Neovim: ASCII never joins */
				else n += enc(c, s + n);
				k++;
			}
		}
		at[k] = n;
		/* the starts uni_glen makes are exactly the code points the file breaks before */
		int j = 0;
		for (size_t i = 0; i < n; i += uni_glen(s, n, i)) {
			while (j < k && at[j] < i) CHECK(!brk[j++]);
			CHECK(j < k && at[j] == i && brk[j]);
			j++;
		}
		while (j < k) CHECK(!brk[j++]);
		cases++;
	}
	fclose(f);
	CHECK(cases == 853);
	printf("test_uni_nvim: GraphemeBreakTest.txt %d of %d cases\n", cases, cases);
}

int main(void)
{
	/* buckets of code points by class and InCB, to draw lines from */
	for (unsigned c = 0x80; c < 0x110000; c++) nbucket[prop(c) % 64]++;
	for (int b = 0; b < 64; b++) { bucket[b] = malloc((nbucket[b] + 1) * sizeof **bucket); nbucket[b] = 0; }
	for (unsigned c = 0x80; c < 0x110000; c++) { unsigned b = prop(c) % 64; bucket[b][nbucket[b]++] = c; }

	static unsigned char lines[LINES][64];
	static size_t len[LINES];
	const char *fixed[] = { "e\xcc\x81", "\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa7",
		"\xf0\x9f\x87\xb3\xf0\x9f\x87\xb1", "\xe4\xb8\xad" };   /* DESIGN.md's four: e+accent, family, flag, CJK */
	FILE *f = fopen("build/uni_nvim.lines", "wb");
	CHECK(f);
	for (int r = 0; r < LINES; r++) {
		if (r < 4) { len[r] = strlen(fixed[r]); memcpy(lines[r], fixed[r], len[r]); }
		else len[r] = line(lines[r]);
		fwrite(lines[r], 1, len[r], f);
		fputc('\n', f);
	}
	fclose(f);
	CHECK(uni_glen(lines[0], 3, 0) == 3 && uni_glen(lines[1], 18, 0) == 18 && uni_glen(lines[2], 8, 0) == 8);
	CHECK(uni_width(lines[3], 3, 0) == 2 && uni_width(lines[1], 18, 0) == 2 && uni_width(lines[2], 8, 0) == 2);

	f = fopen("build/uni_nvim.lua", "w");
	CHECK(f && fputs(lua, f) >= 0);
	fclose(f);
	CHECK(system("nvim --clean --headless -l build/uni_nvim.lua build/uni_nvim") == 0);

	/* 1: every code point */
	f = fopen("build/uni_nvim.props", "rb");
	CHECK(f);
	static unsigned char p[TOP][4];
	CHECK(fread(p, 4, TOP, f) == TOP);
	fclose(f);
	for (unsigned c = 0; c < TOP; c++) {
		unsigned q = prop(c), w1, w2;
		CHECK(p[c][0] <= 20 && cls[p[c][0]] == (int)(q % 16) && p[c][1] == q / 16 % 4);
		unsigned char s[8];
		size_t k = enc(c, s);
		w1 = (unsigned)uni_width(s, k, 0);
		memcpy(s + k, "\xef\xb8\x8f", 3);
		w2 = (unsigned)uni_width(s, k + 3, 0);
		if (w1 != p[c][2] || w2 != p[c][3]) fprintf(stderr, "U+%04X: %u %u, nvim %u %u\n", c, w1, w2, p[c][2], p[c][3]);
		CHECK(w1 == p[c][2] && w2 == p[c][3]);
	}

	/* 2: random lines */
	f = fopen("build/uni_nvim.segs", "r");
	CHECK(f);
	for (int r = 0; r < LINES; r++) {
		const unsigned char *s = lines[r];
		size_t n = len[r], i = 0, b = 0;
		int start[64] = { 0 };
		for (; i < n; i += uni_glen(s, n, i)) {
			unsigned long at, w;
			CHECK(fscanf(f, "%lu %lu", &at, &w) == 2);
			if (at != i) fprintf(stderr, "line %d: cluster at %zu, nvim %lu\n", r + 1, i, at);
			CHECK(at == i && w == (unsigned long)uni_width(s, n, i));
			start[i] = 1;
		}
		CHECK(i == n);
		for (i = 1; i <= n; i++) {
			b = uni_gprev(s, n, i);
			CHECK(start[b] && b < i && i <= b + uni_glen(s, n, b));
		}
	}
	int c;
	while ((c = fgetc(f)) == '\n' || c == ' ') {}
	CHECK(c == EOF);
	fclose(f);
	printf("test_uni_nvim: %u code points exhaustive (class, InCB, width alone and before VS16), %d lines against Neovim\n", TOP, LINES);

	/* 3 */
	graphemetest();
	for (int b = 0; b < 64; b++) free(bucket[b]);
	return 0;
}
