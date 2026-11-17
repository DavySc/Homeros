import re, sys, os
B = '/home/claude/trial/base'
def ed(path, old, new, count=1):
    s = open(path).read()
    assert s.count(old) == count, (path, old[:60], s.count(old))
    open(path, 'w').write(s.replace(old, new))
def dropplants(path, pat):
    L = open(path).read().split('\n')
    k = [l for l in L if not re.search(pat, l)]
    assert len(k) < len(L), pat
    open(path, 'w').write('\n'.join(k))

# T1: uni_gprev removed with its checks
t = B + '/T1'
s = open(t + '/src/uni/uni.c').read()
a = s.index('/* Back to an ASCII byte'); b = s.index('int uni_width')
open(t + '/src/uni/uni.c', 'w').write(s[:a] + s[b:])
ed(t + '/tests/test_uni_nvim.c', ', and uni_gprev from every byte against the starts;', ';')
ed(t + '/tests/test_uni_nvim.c', 'size_t n = len[r], i = 0, b = 0;\n\t\tint start[64] = { 0 };\n', 'size_t n = len[r], i = 0;\n')
ed(t + '/tests/test_uni_nvim.c', '\t\t\tstart[i] = 1;\n', '')
ed(t + '/tests/test_uni_nvim.c', '''		CHECK(i == n);
		for (i = 1; i <= n; i++) {
			b = uni_gprev(s, n, i);
			CHECK(start[b] && b < i && i <= b + uni_glen(s, n, b));
		}
''', '\t\tCHECK(i == n);\n')
f = t + '/tests/fuzz_uni.c'
ed(f, '''   ASCII byte after their first and split an invalid byte from what follows;
   uni_gprev from every byte finds the cluster that holds the byte before;
''', '''   ASCII byte after their first and split an invalid byte from what follows;
''')
ed(f, '''	static unsigned char start[257];
	if (n == 0 || n > 256) return 0;
	for (size_t i = 0; i <= n; i++) start[i] = 0;
''', '''	if (n == 0 || n > 256) return 0;
''')
ed(f, '\t\tstart[i] = 1;\n', '')
ed(f, '''	for (i = 1; i <= n; i++) {
		size_t b = uni_gprev(s, n, i);
		if (!start[b] || b >= i || i > b + uni_glen(s, n, b)) abort();
	}
''', '')
dropplants(t + '/tests/plants/uni.txt', 'uni_gprev')

# T2: uni_width removed with its checks
t = B + '/T2'
s = open(t + '/src/uni/uni.c').read()
a = s.index('int uni_width')
open(t + '/src/uni/uni.c', 'w').write(s[:a].rstrip('\n') + '\n')
f = t + '/tests/test_uni_nvim.c'
ed(f, ', and uni_width alone and before\n      U+FE0F against utf_ptr2cells (exhaustive)', ' (exhaustive)')
ed(f, 'cluster starts and widths against utfc_ptr2len\n      and utf_ptr2cells', 'cluster starts against utfc_ptr2len')
ed(f, '\tCHECK(uni_width(lines[3], 3, 0) == 2 && uni_width(lines[1], 18, 0) == 2 && uni_width(lines[2], 8, 0) == 2);\n', '')
ed(f, '''		unsigned q = prop(c), w1, w2;
		CHECK(p[c][0] <= 20 && cls[p[c][0]] == (int)(q % 16) && p[c][1] == q / 16 % 4);
		unsigned char s[8];
		size_t k = enc(c, s);
		w1 = (unsigned)uni_width(s, k, 0);
		memcpy(s + k, "\\xef\\xb8\\x8f", 3);
		w2 = (unsigned)uni_width(s, k + 3, 0);
		if (w1 != p[c][2] || w2 != p[c][3]) fprintf(stderr, "U+%04X: %u %u, nvim %u %u\\n", c, w1, w2, p[c][2], p[c][3]);
		CHECK(w1 == p[c][2] && w2 == p[c][3]);
''', '''		unsigned q = prop(c);
		CHECK(p[c][0] <= 20 && cls[p[c][0]] == (int)(q % 16) && p[c][1] == q / 16 % 4);
''')
ed(f, 'CHECK(at == i && w == (unsigned long)uni_width(s, n, i));', 'CHECK(at == i);\n\t\t\t(void)w;')
ed(f, '(class, InCB, width alone and before VS16)', '(class, InCB)')
f = t + '/tests/fuzz_uni.c'
ed(f, ';\n   widths are 1, 2, 4 or 6, 1 for ASCII and 4 for an invalid byte.', '.')
ed(f, '\t\tint w = uni_width(s, n, i);\n', '')
ed(f, '\t\tif (k == 1 && s[i] >= 0x80 && (g != 1 || w != 4)) abort();\n\t\tif (s[i] < 0x80 && w != 1) abort();\n\t\tif (w != 1 && w != 2 && w != 4 && w != 6) abort();\n',
   '\t\tif (k == 1 && s[i] >= 0x80 && g != 1) abort();\n')
dropplants(t + '/tests/plants/uni.txt', 'VS16')

# T4: generator and table removed
t = B + '/T4'
os.remove(t + '/src/uni/gen.py'); os.remove(t + '/src/uni/tab.h')

# T5: the Neovim table check removed
t = B + '/T5'
os.remove(t + '/tests/test_uni_nvim.c')
dropplants(t + '/tests/plants/uni.txt', 'TESTS=uni_nvim')
print('ok')
