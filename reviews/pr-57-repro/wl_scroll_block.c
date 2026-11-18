	/* REVIEWER (PR 57, second pass): the cursor line's layer stays on its
	   screen row while the text scrolls under it, heading bars move with
	   the text, the cursor line moves; EVERY frame's screen, drawn
	   incrementally, must equal a whole redraw of the same grid. */
	{
		enum { F = 120 };
		int n = g.rows * g.cols, bad = 0, moves = 0;
		Cell *fr = malloc((size_t)F * (size_t)n * sizeof *fr);
		int cr[F], cc[F], *head = calloc((size_t)g.rows, sizeof *head);
		unsigned s = 12345;
#define R(m) ((s = s * 1103515245u + 12345u) >> 16) % (unsigned)(m)
		CHECK(fr && head);
		grid_clear(&g);
		int line = 0;
		char t[64];
#define LINE(r) do { snprintf(t, sizeof t, "line %d of the text %c%c", line, 'a' + line % 26, 'A' + line % 7); line++; \
		for (int c = 0; c < g.cols; c++) grid_put(&g, r, c, (const unsigned char *)" ", 1, 1, ATTR_NONE); \
		grid_puts(&g, r, 4, t, ATTR_NONE); head[r] = R(6) == 0; \
		for (int c = 4; c < 12; c++) g.c[(r) * g.cols + c].face = (unsigned char)(1 + R(60)); } while (0)
		for (int r = 0; r < g.rows; r++) LINE(r);
		g.crow = g.rows / 2; g.ccol = 4;
		for (int f = 0; f < F; f++) {
			unsigned op = R(4);
			if (f && (op == 0 || op == 2)) {          /* scroll the text by k rows */
				int k = 1 + (int)(R(3)), a = k;
				if (R(2)) k = -k;
				memmove(&g.c[(k < 0 ? a : 0) * g.cols], &g.c[(k > 0 ? a : 0) * g.cols], sizeof *g.c * (size_t)((g.rows - a) * g.cols));
				memmove(&head[k < 0 ? a : 0], &head[k > 0 ? a : 0], sizeof *head * (size_t)(g.rows - a));
				for (int i = 0; i < a; i++) { int r = k > 0 ? g.rows - 1 - i : i; LINE(r); }
				moves++;
			}
			if (f && (op == 1 || op == 2)) g.crow = (g.crow + 1 + (int)(R(5))) % g.rows;   /* the cursor line moves */
			if (f && op == 3) g.ccol = 4 + (int)(R(10));
			for (int r = 0; r < g.rows; r++)
				for (int c = 0; c < g.cols; c++)
					g.c[r * g.cols + c].bg = (unsigned char)(r == g.crow ? FACE_CURSOR_LINE : head[r] ? FACE_HEADING_1_BG : 0);
			memcpy(fr + (size_t)f * (size_t)n, g.c, (size_t)n * sizeof *fr);
			cr[f] = g.crow; cc[f] = g.ccol;
		}
		for (int pass = 0; pass < 2; pass++) {
			grid_free(&shown);
			for (int f = 0; f < F; f++) {
				memcpy(g.c, fr + (size_t)f * (size_t)n, (size_t)n * sizeof *fr);
				g.crow = cr[f]; g.ccol = cc[f];
				if (pass) grid_free(&shown);        /* a whole redraw */
				show();
				pump(30);
				char b[512];
				if (pass) snprintf(b, sizeof b, "grim -t ppm %s/w.ppm && (cmp -s %s/w.ppm %s/f%d.ppm || touch %s/bad%d)", dir, dir, dir, f, dir, f);
				else snprintf(b, sizeof b, "grim -t ppm %s/f%d.ppm", dir, f);
				run(b);
				if (pass) { snprintf(b, sizeof b, "%s/bad%d", dir, f); if (!access(b, F_OK)) { bad++; fprintf(stderr, "REVIEWER: frame %d differs from a whole redraw\n", f); } }
			}
		}
		fprintf(stderr, "REVIEWER: %d frames (%d with a scroll, %dx%d cells), %d differ from a whole redraw\n", F, moves, g.rows, g.cols, bad);
		CHECK(!bad);
		free(fr); free(head);
		grid_clear(&g);
		grid_free(&shown);
		show();
#undef R
#undef LINE
	}

