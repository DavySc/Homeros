/* The frame diff, against a model terminal (test_tty_vt.h): random frames of
   random characters (ASCII, two-byte, wide, four-byte wide), attributes,
   overlapping wide halves, clears and resizes.  After every present the
   model's screen must equal the grid, and the backend must have printed
   exactly the changed cells (plus the two blanks Neovim's
   print_cell_at_pos() puts before a non-ASCII wide one), each frame inside
   one synchronized update.  Then again with a terminal that thinks the
   four-byte characters narrow: the screen must still be right cell for cell.
   Also grid_put's own rules: a two-cell character stays whole (Neovim's
   grid_put_linebuf()). */
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "check.h"
#include "backend.h"
#include "test_tty_vt.h"

static unsigned char out[1 << 20];

/* What each face must look like, written out by hand from catppuccin/nvim
   (Mocha), not computed from the shared table: per face the SGR after the
   attribute that its treesitter.lua gives the capture, with
   its default styles (comments, conditionals and "miscs" italic), links
   followed and unlisted captures falling back to their parent.  Of its
   per-language overrides only the markdown headings are taken (no other
   grammar here has markup.heading.1 to 6); the heading backgrounds are catppuccin's
   render_markdown integration. */
static const char *const face_sgr[FACE_COUNT] = {
	"",
	";38;2;205;214;244",                  /* variable: text */
	";38;2;243;139;168",                  /* variable.builtin: red */
	";38;2;235;160;172",                  /* variable.parameter: maroon */
	";38;2;235;160;172",                  /* variable.parameter.builtin: maroon */
	";38;2;180;190;254",                  /* variable.member: lavender */
	";38;2;250;179;135",                  /* constant: peach */
	";38;2;250;179;135",                  /* constant.builtin: peach */
	";38;2;203;166;247",                  /* constant.macro: mauve */
	";3;38;2;249;226;175",                /* module: yellow, italic */
	";3;38;2;249;226;175",                /* module.builtin: yellow, italic */
	";38;2;116;199;236",                  /* label: sapphire */
	";38;2;166;227;161",                  /* string: green */
	";38;2;148;226;213",                  /* string.documentation: teal */
	";38;2;245;194;231",                  /* string.regexp: pink */
	";38;2;245;194;231",                  /* string.escape: pink */
	";38;2;245;194;231",                  /* string.special: pink */
	";38;2;242;205;205",                  /* string.special.symbol: flamingo */
	";38;2;245;194;231",                  /* string.special.path: pink */
	";3;4;38;2;137;180;250",              /* string.special.url: blue, italic underline */
	";38;2;148;226;213",                  /* character: teal */
	";38;2;245;194;231",                  /* character.special: pink */
	";38;2;250;179;135",                  /* boolean: peach */
	";38;2;250;179;135",                  /* number: peach */
	";38;2;250;179;135",                  /* number.float: peach */
	";38;2;249;226;175",                  /* type: yellow */
	";38;2;203;166;247",                  /* type.builtin: mauve */
	";38;2;249;226;175",                  /* type.definition: yellow */
	";38;2;250;179;135",                  /* attribute: peach */
	";38;2;250;179;135",                  /* attribute.builtin: peach */
	";38;2;180;190;254",                  /* property: lavender */
	";38;2;137;180;250",                  /* function: blue */
	";38;2;250;179;135",                  /* function.builtin: peach */
	";38;2;137;180;250",                  /* function.call: blue */
	";38;2;245;194;231",                  /* function.macro: pink */
	";38;2;137;180;250",                  /* function.method: blue */
	";38;2;137;180;250",                  /* function.method.call: blue */
	";38;2;249;226;175",                  /* constructor: yellow */
	";38;2;137;220;235",                  /* operator: sky */
	";38;2;203;166;247",                  /* keyword: mauve */
	";38;2;203;166;247",                  /* keyword.coroutine: mauve */
	";38;2;203;166;247",                  /* keyword.function: mauve */
	";38;2;203;166;247",                  /* keyword.operator: mauve */
	";38;2;203;166;247",                  /* keyword.import: mauve */
	";38;2;203;166;247",                  /* keyword.type: mauve */
	";38;2;203;166;247",                  /* keyword.modifier: mauve */
	";38;2;203;166;247",                  /* keyword.repeat: mauve */
	";38;2;203;166;247",                  /* keyword.return: mauve */
	";38;2;203;166;247",                  /* keyword.debug: mauve */
	";38;2;203;166;247",                  /* keyword.exception: mauve */
	";3;38;2;203;166;247",                /* keyword.conditional: mauve, italic */
	";38;2;137;220;235",                  /* keyword.conditional.ternary: sky */
	";38;2;245;194;231",                  /* keyword.directive: pink */
	";38;2;245;194;231",                  /* keyword.directive.define: pink */
	";38;2;147;153;178",                  /* punctuation.delimiter: overlay2 */
	";38;2;147;153;178",                  /* punctuation.bracket: overlay2 */
	";38;2;245;194;231",                  /* punctuation.special: pink */
	";3;38;2;147;153;178",                /* comment: overlay2, italic */
	";3;38;2;147;153;178",                /* comment.documentation: overlay2, italic */
	";38;2;30;30;46;48;2;243;139;168",    /* comment.error: base on red */
	";38;2;30;30;46;48;2;249;226;175",    /* comment.warning: base on yellow */
	";38;2;30;30;46;48;2;242;205;205",    /* comment.todo: base on flamingo */
	";38;2;30;30;46;48;2;245;224;220",    /* comment.note: base on rosewater */
	";1;38;2;243;139;168",                /* markup.strong: red, bold */
	";3;38;2;243;139;168",                /* markup.italic: red, italic */
	";9;38;2;205;214;244",                /* markup.strikethrough: text, strikethrough */
	";4",                                 /* markup.underline: underline */
	";38;2;137;180;250",                  /* markup.heading: blue */
	";38;2;243;139;168",                  /* markup.heading.1: red (@markup.heading.1.markdown) */
	";38;2;250;179;135",                  /* markup.heading.2: peach (@markup.heading.2.markdown) */
	";38;2;249;226;175",                  /* markup.heading.3: yellow (@markup.heading.3.markdown) */
	";38;2;166;227;161",                  /* markup.heading.4: green (@markup.heading.4.markdown) */
	";38;2;116;199;236",                  /* markup.heading.5: sapphire (@markup.heading.5.markdown) */
	";38;2;180;190;254",                  /* markup.heading.6: lavender (@markup.heading.6.markdown) */
	";38;2;245;194;231",                  /* markup.quote: pink */
	";38;2;137;180;250",                  /* markup.math: blue */
	";38;2;180;190;254",                  /* markup.link: lavender */
	";38;2;180;190;254",                  /* markup.link.label: lavender */
	";3;4;38;2;137;180;250",              /* markup.link.url: blue, italic underline */
	";38;2;166;227;161",                  /* markup.raw: green */
	";38;2;166;227;161",                  /* markup.raw.block: green */
	";38;2;148;226;213",                  /* markup.list: teal */
	";38;2;166;227;161",                  /* markup.list.checked: green */
	";38;2;127;132;156",                  /* markup.list.unchecked: overlay1 */
	";38;2;166;227;161",                  /* diff.plus: green */
	";38;2;243;139;168",                  /* diff.minus: red */
	";38;2;137;180;250",                  /* diff.delta: blue */
	";38;2;137;180;250",                  /* tag: blue */
	";38;2;137;180;250",                  /* tag.builtin: blue */
	";3;38;2;249;226;175",                /* tag.attribute: yellow, italic */
	";38;2;148;226;213",                  /* tag.delimiter: teal */
	";48;2;90;61;80",                    /* heading 1 background */
	";48;2;92;72;71",                    /* heading 2 background */
	";48;2;91;85;82",                    /* heading 3 background */
	";48;2;68;85;78",                    /* heading 4 background */
	";48;2;54;77;99",                    /* heading 5 background */
	";48;2;72;75;104",                    /* heading 6 background */
	";48;2;42;43;60",                     /* cursor line */
	";38;2;180;190;254",                  /* cursor line nr */
	";38;2;69;71;90",                     /* line nr */
	";38;2;205;214;244;48;2;24;24;37",    /* status line */
	";38;2;137;180;250;48;2;24;24;37",    /* status line nc */
	";38;2;17;17;27",                     /* win separator */
	";38;2;69;71;90",                     /* end of buffer */
	";1;48;2;69;71;90",                   /* visual */
	";38;2;205;214;244;48;2;62;87;103",   /* search */
	";38;2;24;24;37;48;2;243;139;168",    /* cur search */
	";1;38;2;250;179;135;48;2;57;59;77",  /* match paren */
	";38;2;147;153;178;48;2;24;24;37",    /* pmenu */
	";1;48;2;49;50;68",                   /* pmenu sel */
	";38;2;205;214;244;48;2;24;24;37",    /* normal float */
	";38;2;137;180;250;48;2;24;24;37",    /* float border */
	";38;2;166;173;200;48;2;24;24;37",    /* float title */
	";38;2;30;30;46;48;2;245;224;220",    /* cursor */
	";1;38;2;24;24;37;48;2;137;180;250",  /* mode normal */
	";1;38;2;30;30;46;48;2;166;227;161",  /* mode insert */
	";1;38;2;30;30;46;48;2;203;166;247",  /* mode visual */
	";1;38;2;30;30;46;48;2;243;139;168",  /* mode replace */
	";1;38;2;30;30;46;48;2;250;179;135",  /* mode command */
	";1;38;2;30;30;46;48;2;148;226;213",  /* mode other */
	";38;2;186;194;222;48;2;69;71;90",    /* status info */
	";38;2;166;227;161",                  /* git add */
	";38;2;249;226;175",                  /* git change */
	";38;2;243;139;168",                  /* git delete */
	";3;38;2;243;139;168",                /* diag error */
	";3;38;2;249;226;175",                /* diag warn */
	";3;38;2;137;220;235",                /* diag info */
	";3;38;2;148;226;213",                /* diag hint */
	"",                                   /* diag under error (the terminal draws no undercurl yet) */
	"",                                   /* diag under warn (the terminal draws no undercurl yet) */
	"",                                   /* diag under info (the terminal draws no undercurl yet) */
	"",                                   /* diag under hint (the terminal draws no undercurl yet) */
	";3;38;2;243;139;168;48;2;50;40;58",  /* diag vt error */
	";3;38;2;249;226;175;48;2;51;49;58",  /* diag vt warn */
	";3;38;2;137;220;235;48;2;40;48;64",  /* diag vt info */
	";3;38;2;148;226;213;48;2;41;49;62",  /* diag vt hint */
	";38;2;49;50;68",                     /* indent */
	";38;2;147;153;178",                  /* indent scope */
	";38;2;137;180;250",                  /* pick match */
	";38;2;242;205;205",                  /* pick prompt */
	";38;2;137;180;250",                  /* which-key group */
	";38;2;245;194;231",                  /* which-key desc */
	";38;2;108;112;134",                  /* which-key separator */
	";1;38;2;203;166;247",                /* kitten cat: mauve */
	";1;38;2;166;227;161",                /* kitten face: green */
	";1;38;2;250;179;135",                /* kitten paw: peach */
	";1;38;2;243;139;168",                /* kitten heart: red */
	";1;38;2;245;194;231",                /* kitten text: pink */
	";38;2;108;112;134",                  /* kitten dim: overlay0 */
};

/* backend_present's output, through a file: a frame may be larger than a pipe holds */
static size_t present(const Grid *g, Grid *shown)
{
	FILE *f = tmpfile();
	int saved = dup(1);
	CHECK(f && saved >= 0);
	dup2(fileno(f), 1);
	backend_tty.present(g, shown);
	dup2(saved, 1);
	close(saved);
	rewind(f);
	size_t n = fread(out, 1, sizeof out, f);
	CHECK(n < sizeof out);
	fclose(f);
	return n;
}

/* characters and clusters: ASCII, two-byte, wide, four-byte wide, e with a
   combining accent (in the cell), e with five (in the store), a ZWJ family
   (wide, in the store), e with twenty (41 bytes: cut to 31) */
#define M5 "\xcc\x81\xcc\x81\xcc\x81\xcc\x81\xcc\x81"
static const char *const chars[] = { "a", "b", "~", " ", "\xc3\xa9", "\xce\xbb", "\xe6\xbc\xa2", "\xe2\x88\x80", "\xf0\x9f\x98\x80", "\xf0\x9d\x93\x9d",
                                     "e\xcc\x81", "e" M5, "\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa7",
                                     "e" M5 M5 M5 M5 };
static const int widths[] =         { 1,   1,   1,   1,   1,          1,          2,              1,              2,                  1,
                                     1, 1, 2, 1 };

/* the terminal's width of one code point: combining accent and ZWJ 0,
   CJK and emoji 2 */
static int same_width(const unsigned char *s, int n)
{
	static const char *const zero[] = { "\xcc\x81", "\xe2\x80\x8d" };
	static const char *const two[] = { "\xe6\xbc\xa2", "\xf0\x9f\x98\x80", "\xf0\x9f\x91\xa8", "\xf0\x9f\x91\xa9", "\xf0\x9f\x91\xa7" };
	for (size_t i = 0; i < sizeof zero / sizeof *zero; i++) if ((int)strlen(zero[i]) == n && !memcmp(zero[i], s, (size_t)n)) return 0;
	for (size_t i = 0; i < sizeof two / sizeof *two; i++) if ((int)strlen(two[i]) == n && !memcmp(two[i], s, (size_t)n)) return 2;
	return 1;
}

/* a terminal older than the grid's tables: four-byte characters are narrow */
static int narrow_width(const unsigned char *s, int n) { return n == 4 ? 1 : same_width(s, n); }

static void invariant(const Grid *g)
{
	for (int r = 0; r < g->rows; r++)
		for (int c = 0; c < g->cols; c++) {
			const Cell *x = &g->c[r * g->cols + c];
			if (x->len) continue;
			/* a right half follows a left half that starts a wide character */
			CHECK(c > 0 && x[-1].len > 0 && x[-1].attr == x->attr);
		}
}

static void run(int (*width)(const unsigned char *, int), int frames)
{
	static Vt t;
	static Grid prev;
	Grid g = { 0 }, shown = { 0 };
	int rows = 2 + (int)rnd(10), cols = 2 + (int)rnd(30);
	CHECK(!grid_resize(&g, rows, cols));
	vt_init(&t, rows, cols, width);
	for (int f = 0; f < frames; f++) {
		if (!rnd(40)) {          /* resize: the whole screen is sent again */
			rows = 1 + (int)rnd(12); cols = 1 + (int)rnd(40);
			CHECK(!grid_resize(&g, rows, cols));
			/* the terminal's old content does not matter: a new size is drawn whole */
			if (rows != t.rows || cols != t.cols) vt_resize(&t, rows, cols);
		} else if (!rnd(20)) grid_clear(&g);
		for (int k = (int)rnd(3) ? (int)rnd(8) : (int)rnd(200); k > 0; k--) {
			unsigned i = rnd(sizeof chars / sizeof *chars);
			int r = (int)rnd((unsigned)rows + 1) - (rnd(9) == 0), c = (int)rnd((unsigned)cols + 1) - (rnd(9) == 0);
			int w = grid_put(&g, r, c, (const unsigned char *)chars[i], strlen(chars[i]), widths[i], (int)rnd(3));
			CHECK(w == (r >= 0 && r < rows && c >= 0 && c + widths[i] <= cols ? widths[i] : 0));
			if (w && rnd(2)) {        /* the core sets a face after grid_put */
				unsigned char f = (unsigned char)rnd(FACE_COUNT);
				g.c[r * cols + c].face = f;
				if (w == 2) g.c[r * cols + c + 1].face = f;
			}
			if (w && rnd(2)) {        /* and a background layer */
				unsigned char b = rnd(2) ? 0 : (unsigned char)(FACE_HEADING_1_BG + rnd(6));
				g.c[r * cols + c].bg = b;
				if (w == 2) g.c[r * cols + c + 1].bg = b;
			}
			invariant(&g);
		}
		g.crow = (int)rnd((unsigned)rows);
		g.ccol = (int)rnd((unsigned)cols);
		/* the cells that changed since the last frame, with Neovim's blanks */
		if (prev.rows != rows || prev.cols != cols) CHECK(!grid_resize(&prev, rows, cols));
		int want = 0;
		for (int i = 0; i < rows * cols; i++)
			if (g.c[i].len && !grid_cell_eq(&g.c[i], &prev.c[i]))
				want += 1 + 2 * (g.c[i].len > 1 && i % cols + 1 < cols && !g.c[i + 1].len);
		int before = t.prints, fr = t.frames;
		size_t n = present(&g, &shown);
		vt_feed(&t, out, n);
		const char *why = vt_differs(&t, &g);
		if (why) {
			fprintf(stderr, "frame %d (%dx%d): %s\n", f, rows, cols, why);
			CHECK(0);
		}
		CHECK(t.frames == fr + 1);
		/* a cell that is part of an unchanged character is never sent */
		CHECK(t.prints - before == want);
		memcpy(prev.c, g.c, (size_t)(rows * cols) * sizeof *g.c);
	}
	grid_free(&g);
	grid_free(&shown);
	grid_free(&prev);
}

int main(void)
{
	/* the model numbers each face's SGR; the backend must send exactly that */
	for (int f = 0; f < FACE_COUNT; f++) vt_face[f] = vt_style_id(face_sgr[f]);
	/* under a bg layer: the face's SGR without its background, then the
	   layer's (which is a background only) */
	for (int b = FACE_HEADING_1_BG; b <= FACE_HEADING_6_BG; b++)
		for (int f = 0; f < FACE_COUNT; f++) {
			char s[64];
			const char *bg = strstr(face_sgr[f], ";48;");
			snprintf(s, sizeof s, "%.*s%s", bg ? (int)(bg - face_sgr[f]) : (int)strlen(face_sgr[f]), face_sgr[f], face_sgr[b]);
			vt_layer[b][f] = vt_style_id(s);
		}

	/* grid_put's rules, one case each */
	Grid g = { 0 };
	CHECK(!grid_resize(&g, 1, 6));
	const unsigned char *wide = (const unsigned char *)"\xe6\xbc\xa2";
	/* the '>' and the blank keep the attribute, face and background layer
	   of the half they replace */
	CHECK(grid_put(&g, 0, 1, wide, 3, 2, ATTR_REV) == 2);
	g.c[1].face = g.c[2].face = FACE_STRING;
	g.c[1].bg = g.c[2].bg = FACE_CURSOR_LINE;
	CHECK(grid_put(&g, 0, 2, (const unsigned char *)"x", 1, 1, ATTR_NONE) == 1);   /* right half */
	CHECK(g.c[1].len == 1 && g.c[1].ch[0] == '>' && g.c[1].attr == ATTR_REV && g.c[1].face == FACE_STRING && g.c[1].bg == FACE_CURSOR_LINE && g.c[2].ch[0] == 'x');
	CHECK(grid_put(&g, 0, 3, wide, 3, 2, ATTR_DIM) == 2);
	g.c[3].face = g.c[4].face = FACE_COMMENT;
	g.c[3].bg = g.c[4].bg = FACE_HEADING_1_BG;
	CHECK(grid_put(&g, 0, 3, (const unsigned char *)"y", 1, 1, ATTR_NONE) == 1);   /* left half */
	CHECK(g.c[4].len == 1 && g.c[4].ch[0] == ' ' && g.c[4].attr == ATTR_DIM && g.c[4].face == FACE_COMMENT && g.c[4].bg == FACE_HEADING_1_BG);
	CHECK(grid_put(&g, 0, 4, wide, 3, 2, ATTR_NONE) == 2);
	CHECK(grid_put(&g, 0, 3, wide, 3, 2, ATTR_DIM) == 2);                           /* overlapping wide */
	CHECK(g.c[3].len == 3 && !g.c[4].len && g.c[5].len == 1 && g.c[5].ch[0] == ' ');
	CHECK(!grid_put(&g, 0, 5, wide, 3, 2, ATTR_NONE));
	Cell was = g.c[0];
	CHECK(!grid_put(&g, 0, 0, wide, 3, 0, ATTR_NONE) && !grid_put(&g, 0, 0, wide, 3, 3, ATTR_NONE));
	CHECK(grid_cell_eq(&g.c[0], &was) && g.c[1].ch[0] == '>');                     /* widths 0, 3 refused */
	CHECK(grid_resize(&g, 1 << 16, 1 << 16) == -1 && g.rows == 1);                  /* overflow refused */
	g.crow = g.ccol = 3;
	CHECK(!grid_resize(&g, 2, 2) && !g.crow && !g.ccol);

	/* clusters: up to 4 bytes in the cell; a longer one by number, equal
	   clusters equal numbers; one past GRID_CLUSTER_MAX (31) cut to whole
	   code points, as nvim 0.12.6 shows them (measured, 3 runs each: e and
	   20 accents 31 bytes, a 39-byte ZWJ chain of men 28) */
	CHECK(!grid_resize(&g, 2, 6));
	const char *chain = "\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa8\xe2\x80\x8d"
	                    "\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa8";
	CHECK(grid_put(&g, 0, 0, (const unsigned char *)"e\xcc\x81", 3, 1, ATTR_NONE) == 1 && !memcmp(g.c[0].ch, "e\xcc\x81", 3));
	CHECK(grid_put(&g, 0, 1, (const unsigned char *)chars[11], 11, 1, ATTR_NONE) == 1);
	CHECK(grid_put(&g, 1, 1, (const unsigned char *)chars[11], 11, 1, ATTR_NONE) == 1);
	CHECK(grid_put(&g, 0, 2, (const unsigned char *)chars[13], 41, 1, ATTR_NONE) == 1);
	CHECK(grid_put(&g, 0, 3, (const unsigned char *)chain, 39, 2, ATTR_NONE) == 2);
	CHECK(g.c[1].len == 11 && !memcmp(grid_text(&g.c[1]), chars[11], 11) && grid_cell_eq(&g.c[1], &g.c[7]));
	CHECK(g.c[2].len == 31 && !memcmp(grid_text(&g.c[2]), chars[13], 31) && !grid_cell_eq(&g.c[1], &g.c[2]));
	CHECK(g.c[3].len == 28 && !memcmp(grid_text(&g.c[3]), chain, 28) && !g.c[4].len);
	CHECK(grid_text(&g.c[0]) == g.c[0].ch);
	{   /* exactly 31 bytes (e and 15 accents): kept whole, nothing read past it */
		unsigned char *e15 = malloc(31);
		CHECK(e15 != NULL);
		memcpy(e15, "e" M5 M5 M5, 31);
		CHECK(grid_put(&g, 1, 3, e15, 31, 1, ATTR_NONE) == 1 && g.c[9].len == 31 && !memcmp(grid_text(&g.c[9]), e15, 31));
		/* a stored cluster's prefix is a cluster of its own */
		CHECK(grid_put(&g, 1, 4, e15, 29, 1, ATTR_NONE) == 1 && g.c[10].len == 29 && !grid_cell_eq(&g.c[9], &g.c[10]));
		free(e15);
	}
	CHECK(grid_put(&g, 1, 2, (const unsigned char *)"a" M5, 11, 1, ATTR_NONE) == 1);   /* same length, other bytes */
	CHECK(!grid_cell_eq(&g.c[1], &g.c[8]) && !memcmp(grid_text(&g.c[8]), "a" M5, 11));

	/* the store: emptied by grid_frame once over half of its 1 << 17 slots
	   are taken; full in a frame, a cluster shows as U+FFFD */
	CHECK(!grid_frame());
	char k[16];
	for (int i = 0; i < (1 << 16) - 5; i++) {           /* with the 5 above (e15 is the cut one): half */
		snprintf(k, sizeof k, "z%07d", i);
		grid_put(&g, 1, 0, (const unsigned char *)k, 8, 1, ATTR_NONE);
	}
	CHECK(!grid_frame());
	CHECK(grid_put(&g, 1, 0, (const unsigned char *)"zlast-one", 9, 1, ATTR_NONE) == 1);
	CHECK(grid_frame() && !grid_frame());
	for (int i = 0; i < (1 << 17) - 1; i++) {
		snprintf(k, sizeof k, "y%07d", i);
		grid_put(&g, 1, 0, (const unsigned char *)k, 8, 1, ATTR_NONE);
		CHECK(g.c[6].len == 8 && !memcmp(grid_text(&g.c[6]), k, 8));
	}
	CHECK(grid_put(&g, 1, 0, (const unsigned char *)"y-too-many", 10, 1, ATTR_NONE) == 1);
	CHECK(g.c[6].len == 3 && !memcmp(g.c[6].ch, "\xef\xbf\xbd", 3));
	/* nor is a prefix of a stored one (its probe walks most of the full
	   table, past y0000000 to y0000009) */
	CHECK(grid_put(&g, 1, 0, (const unsigned char *)"y000000", 7, 1, ATTR_NONE) == 1);
	CHECK(g.c[6].len == 3 && !memcmp(g.c[6].ch, "\xef\xbf\xbd", 3));
	snprintf(k, sizeof k, "y%07d", 5);                  /* a stored one is still found */
	grid_put(&g, 1, 0, (const unsigned char *)k, 8, 1, ATTR_NONE);
	CHECK(g.c[6].len == 8 && !memcmp(grid_text(&g.c[6]), k, 8));
	CHECK(grid_frame() && !grid_frame());
	grid_free(&g);

	for (int i = 0; i < 300; i++) run(same_width, 60);
	for (int i = 0; i < 100; i++) run(narrow_width, 60);

	/* a frame the shown grid cannot take (its size overflows) still ends
	   the synchronized update */
	static Vt t;
	Grid huge = { .rows = 1 << 16, .cols = 1 << 16 }, shown = { 0 };
	vt_init(&t, 1, 1, same_width);
	size_t n = present(&huge, &shown);
	vt_feed(&t, out, n);
	CHECK(!t.bad && !t.sync && t.frames == 1 && !t.hidden);

	/* frames larger than the 64 KiB output buffer: a 64x256 screen of wide
	   and non-ASCII characters, drawn whole three times */
	CHECK(!grid_resize(&g, 64, 256));
	vt_init(&t, 64, 256, same_width);
	for (int f = 0; f < 3; f++) {
		for (int r = 0; r < 64; r++)
			for (int c = 0; c + 1 < 256; c += 2) {
				const char *ch = chars[4 + (unsigned)(r + c + f) % 6];
				int w = same_width((const unsigned char *)ch, (int)strlen(ch));
				grid_put(&g, r, c, (const unsigned char *)ch, strlen(ch), w, f % 3);
				if (w == 1) grid_put(&g, r, c + 1, (const unsigned char *)chars[5], 2, 1, f % 3);
			}
		n = present(&g, &shown);
		CHECK(n > 1 << 16);
		vt_feed(&t, out, n);
		CHECK(!vt_differs(&t, &g));
	}
	grid_free(&g);
	grid_free(&shown);

	puts("test_grid_diff: 24000 random frames with random faces, background layers and clusters, and 3 of over 64 KiB, shown exactly by a model terminal, only changed cells sent; wide pairs kept whole; long clusters cut as nvim cuts them, stored once, the store emptied past half");
	return 0;
}
