/* probe FILE KEYS [hl]: feeds KEYS (\r Enter, \e Esc, \xNN) to the keys, prints
   the cursor, message and (short) text; with hl, times keys_hl over the window */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "core.h"
#include "keys.h"
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }
int main(int argc, char **argv)
{
	static Editor e;
	if (argc < 3 || core_open(&e, argv[1])) return 2;
	double t0 = now();
	for (const char *s = argv[2]; *s; s++) {
		int c = (unsigned char)*s;
		if (c == '\\' && s[1] == 'r') c = KEY_ENTER, s++;
		else if (c == '\\' && s[1] == 'e') c = KEY_ESC, s++;
		else if (c == '\\' && s[1] == 'x') { char h[3] = { s[2], s[3], 0 }; c = (int)strtol(h, NULL, 16); s += 3; }
		keys_feed(&e, c, s[1] != 0);
	}
	printf("line %zu col %zu msg [%s] keys %.3f s\n", e.win->line + 1, e.win->col, e.msg, now() - t0);
	if (argc > 3) {
		t0 = now();
		int n = 0;
		for (size_t l = e.win->top; l < core_lines(&e) && l < e.win->top + 50; l++) {
			size_t s, z, len;
			core_line(&e, l, &len);
			for (size_t c = 0; c <= len && keys_hl(&e, e.win, l, c, &s, &z); c = z > s ? z : s + 1) { n++; if (argv[3][0] == 'v') printf(" hl %zu [%zu,%zu)\n", l, s, z); }
		}
		printf("hl: %d matches, one frame of 50 lines %.3f s\n", n, now() - t0);
	}
	if (core_lines(&e) < 200) for (size_t l = 0; l < core_lines(&e); l++) { size_t n; const unsigned char *p = core_line(&e, l, &n); printf("|%.*s|\n", (int)n, p); }
	return 0;
}
