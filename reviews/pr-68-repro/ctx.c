/* Does a run shape the same without the blank before it?  span() starts a
   changed span at a word start, so draw_span shapes "/\ y" where a whole
   redraw shapes "x /\ y".  Prints each glyph's cell, width and left edge. */
#include <stdio.h>
#include <string.h>
#include "font.h"
int main(void)
{
	static const char *const T[] = { "x /\\ y", "/\\ y", " /\\ y", "x /\\", "/\\", "x \\/ y", "\\/ y", "x -> y", "-> y", "x => y", "=> y", "x <= y", "<= y", "x != y", "!= y", "x |- y", "|- y", "x :: y", ":: y", "x <-> y", "<-> y" };
	Placed pl[32];
	if (font_open("JetBrains Mono", 16)) return 2;
	for (size_t i = 0; i < sizeof T / sizeof *T; i++) {
		int m = font_shape((const unsigned char *)T[i], strlen(T[i]), pl, 32);
		printf("%-10s", T[i]);
		for (int j = 0; j < m; j++) if (T[i][pl[j].cluster] != ' ' && T[i][pl[j].cluster] != 'x' && T[i][pl[j].cluster] != 'y') printf("  '%c' w %d left %d", T[i][pl[j].cluster], pl[j].g->w, pl[j].x + pl[j].g->left);
		printf("\n");
	}
	font_close();
	return 0;
}
