/* reads gb3.lua's records; uni_glen's starts and uni_gprev against them */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "uni.h"
int main(int argc, char **argv)
{
	FILE *f = fopen(argv[1], "r");
	char l[512];
	long cases = 0, bad = 0, badprev = 0, cr = 0;
	(void)argc;
	while (fgets(l, sizeof l, f)) {
		unsigned char s[64]; size_t n = 0; char *p = l;
		for (; *p != ' '; p += 2) { unsigned x; sscanf(p, "%2x", &x); s[n++] = (unsigned char)x; }
		int nv[64] = { 0 }, us[64] = { 0 };
		for (char *q = strtok(p, " \n"); q; q = strtok(NULL, " \n")) nv[atoi(q)] = 1;
		for (size_t i = 0; i < n; i += uni_glen(s, n, i)) us[i] = 1;
		cases++;
		if (memcmp(nv, us, sizeof nv)) { if (bad++ < 5) printf("differs: %s", l); }
		for (size_t i = 0; i + 2 < n; i++) if (s[i] == 0x0d && us[i] && !us[i + 1] && uni_glen(s, n, i) >= 3) { cr++; break; }
		for (size_t i = 1; i <= n; i++) {
			size_t b = uni_gprev(s, n, i);
			if (!(us[b] && b < i && i <= b + uni_glen(s, n, b))) { if (badprev++ < 5) printf("gprev(%zu)=%zu: %s", i, b, l); }
		}
	}
	printf("%ld sequences, %ld differ from Neovim, %ld uni_gprev wrong, %ld hold a CR cluster of 3 or more bytes\n", cases, bad, badprev, cr);
	return bad || badprev;
}
