"""Finding 5: writes OUT, tests/test_git_win.c with its watch part replaced
by: an ignored change, 200 ms, a change that counts, 200 ms, then one
git_drain (as write.c's before a write) and a second one.
  python3 reviews/pr-70-repro/drain_win.py OUT.c
Build it as src/git/check.sh builds test_git_win (add -Itests), run under Wine."""
import sys
s = open('tests/test_git_win.c').read()
a = s.index('\tput("a.txt", "a\\n");'); b = s.index('\tputs("test_git_win:')
probe = r'''	for (int k = 0; k < 5; k++) {
		char nm[32];
		snprintf(nm, sizeof nm, "build/x%d.o", k);
		put(nm, "x");
		Sleep(200);
		put("a.txt", k & 1 ? "p\n" : "q\n");
		Sleep(200);
		unsigned long v = r->ver;
		git_drain(r);
		unsigned long v1 = r->ver;
		Sleep(200);
		git_drain(r);
		printf("round %d: first drain moved %lu, second drain moved %lu\n", k, v1 - v, r->ver - v1);
		seen(r, 500);
	}
	git_unwatch(r);
'''
open(sys.argv[1], 'w').write(s[:a] + probe + s[b:].replace('puts("test_git_win:', 'puts("probe done:'))
