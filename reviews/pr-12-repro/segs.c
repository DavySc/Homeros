/* Review of PR 12: prints, per input line, "start:width ..." by uni_glen and
   uni_width, then "| b1 b2 ..." = uni_gprev(i) for i = 1..n.
   cc -Iinclude reviews/pr-12-repro/segs.c src/uni/uni.c -o build/segs */
#include <stdio.h>
#include <string.h>
#include "uni.h"
int main(void)
{
	static unsigned char s[1 << 16];
	while (fgets((char *)s, sizeof s, stdin)) {
		size_t n = strlen((char *)s);
		if (n && s[n - 1] == '\n') n--;
		for (size_t i = 0; i < n; i += uni_glen(s, n, i)) printf("%zu:%d ", i, uni_width(s, n, i));
		printf("|");
		for (size_t i = 1; i <= n; i++) printf(" %zu", uni_gprev(s, n, i));
		printf("\n");
	}
	return 0;
}
