/* Repro: ATTR_GUIDE (grid.h, new in PR 57) has the value of the Wayland
   backend's private ATTR_PRE (ATTR_REV + 1), so the backend draws a guide
   cell as a preedit: underlined.  Counts the pixels in the cell's last row
   that are not the background. */
#include "wl.c"
int main(void)
{
	Grid g = { 0 };
	if (font(16) || grid_resize(&g, 1, 4)) return 2;
	bw = 4 * cell_w; bh = cell_h;
	uint32_t *px = calloc((size_t)(bw * bh), sizeof *px);
	g.crow = 0; g.ccol = 3;                       /* cursor away from cells 0 and 1 */
	grid_put(&g, 0, 0, (const unsigned char *)" ", 1, 1, ATTR_NONE);
	grid_put(&g, 0, 1, (const unsigned char *)" ", 1, 1, ATTR_NONE); g.c[1].bg = FACE_CURSOR_LINE; g.c[1].face = FACE_COMMENT_TODO;
	
	draw_span(px, &g, 0, 0, 3);
	int n[2] = { 0, 0 };
	for (int c = 0; c < 2; c++)
		for (int x = 0; x < cell_w; x++) n[c] += px[(cell_h - 1) * bw + c * cell_w + x] != BG;
	printf("wl: cell with face comment.todo (bg f2cdcd) under layer cursor line (2a2b3c): top-left pixel %06x\n", px[cell_w] & 0xffffff); return (px[cell_w] & 0xffffff) != 0x2a2b3c;
	
}
