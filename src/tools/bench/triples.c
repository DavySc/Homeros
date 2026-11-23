/* For thread 0's question on faces[256] (2026-11-20): how many distinct
   colour and style combinations a program puts on one screen.  Feeds a
   recording (record.py) to libghostty-vt in 4 KB pieces and after each
   counts the combinations term.c would give a face slot (fg, bg, bold,
   italic, strike, underline kind; default colours and no style take none).
   Prints the most on any one screen, and the most on two screens in a row
   (what term.c's slots must hold: a slot the shown screen uses waits a
   frame before it is reused).
   Usage: triples ROWS COLS FILE */
#include <ghostty/vt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { int hf, hb; GhosttyColorRgb f, b; unsigned st; } Key;

int main(int argc, char **argv)
{
	if (argc != 4) return 2;
	int rows = atoi(argv[1]), cols = atoi(argv[2]), best = 0, best2 = 0, np = 0;
	FILE *in = fopen(argv[3], "rb");
	GhosttyTerminal t;
	GhosttyRenderState rs;
	GhosttyRenderStateRowIterator it;
	GhosttyRenderStateRowCells cells;
	if (!in || ghostty_terminal_new(NULL, &t, (uint16_t)cols, (uint16_t)rows) || ghostty_render_state_new(NULL, &rs)
	    || ghostty_render_state_row_iterator_new(NULL, &it) || ghostty_render_state_row_cells_new(NULL, &cells)) return 1;
	static Key seen[1 << 16], prev[1 << 16];
	unsigned char buf[4096];
	for (size_t n; (n = fread(buf, 1, sizeof buf, in)) > 0;) {
		ghostty_terminal_vt_write(t, buf, n);
		ghostty_render_state_update(rs, t);
		ghostty_render_state_get(rs, GHOSTTY_RENDER_STATE_DATA_ROW_ITERATOR, &it);
		int ns = 0;
		while (ghostty_render_state_row_iterator_next(it)) {
			ghostty_render_state_row_get(it, GHOSTTY_RENDER_STATE_ROW_DATA_CELLS, &cells);
			while (ghostty_render_state_row_cells_next(cells)) {
				Key k = { 0 };
				GhosttyStyle s;
				ghostty_render_state_row_cells_get(cells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_STYLE, &s);
				k.hf = !ghostty_render_state_row_cells_get(cells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_FG_COLOR, &k.f);
				k.hb = !ghostty_render_state_row_cells_get(cells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_BG_COLOR, &k.b);
				k.st = s.flags & (GHOSTTY_STYLE_FLAG_BOLD | GHOSTTY_STYLE_FLAG_ITALIC | GHOSTTY_STYLE_FLAG_STRIKETHROUGH
				                  | GHOSTTY_STYLE_FLAG_INVERSE | GHOSTTY_STYLE_FLAG_INVISIBLE | GHOSTTY_STYLE_UNDERLINE_MASK);
				if (!k.hf && !k.hb && !k.st) continue;
				int i = 0;
				while (i < ns && memcmp(&seen[i], &k, sizeof k)) i++;
				if (i == ns && ns < (int)(sizeof seen / sizeof *seen)) seen[ns++] = k;
			}
		}
		int nu = ns;
		for (int j = 0; j < np; j++) {
			int i = 0;
			while (i < ns && memcmp(&seen[i], &prev[j], sizeof prev[j])) i++;
			nu += i == ns;
		}
		if (ns > best) best = ns;
		if (nu > best2) best2 = nu;
		memcpy(prev, seen, ns * sizeof *seen);
		np = ns;
	}
	printf("%d %d\n", best, best2);
	return 0;
}
