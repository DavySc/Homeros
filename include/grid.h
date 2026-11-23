/* The cell grid: what the core draws and a backend shows.  The core fills a
   grid each frame; a backend compares it with the last frame it showed and
   sends only changed cells (Neovim's grid, foot).  No allocation after
   grid_resize, so the redraw path allocates nothing. */
#ifndef GRID_H
#define GRID_H
#include <stddef.h>

/* ATTR_DIM and ATTR_REV are Neovim's default look (line numbers, status
   line); they go once the core draws the UI faces below.  ATTR_GUIDE: an
   indent guide, a 1 px line at the cell's left edge in its face's fg (the
   cell's text stays a space, so screen traces do not change; snacks.indent
   draws the same with a character).  ATTR_IMAGE: the cell shows a
   cell-sized tile of an inline image (image.h), not text; ch (len 4, set
   only by image_put) holds the image's number, the tile's column and row
   and its cache slot's generation, so grid_cell_eq and the backends' row
   hashes see a changed or reused tile with no code of their own. */
enum { ATTR_NONE, ATTR_DIM, ATTR_REV, ATTR_GUIDE, ATTR_IMAGE };
/* Syntax faces (package 12): Neovim's standard tree-sitter captures
   (:help treesitter-highlight-groups), in its order, "." as "_"; a capture
   with a longer name takes its longest listed prefix, as Neovim's highlight
   groups fall back.  A backend maps each to a colour of the theme. */
enum {
	FACE_NONE, FACE_VARIABLE, FACE_VARIABLE_BUILTIN, FACE_VARIABLE_PARAMETER,
	FACE_VARIABLE_PARAMETER_BUILTIN, FACE_VARIABLE_MEMBER, FACE_CONSTANT,
	FACE_CONSTANT_BUILTIN, FACE_CONSTANT_MACRO, FACE_MODULE, FACE_MODULE_BUILTIN,
	FACE_LABEL, FACE_STRING, FACE_STRING_DOCUMENTATION, FACE_STRING_REGEXP,
	FACE_STRING_ESCAPE, FACE_STRING_SPECIAL, FACE_STRING_SPECIAL_SYMBOL,
	FACE_STRING_SPECIAL_PATH, FACE_STRING_SPECIAL_URL, FACE_CHARACTER,
	FACE_CHARACTER_SPECIAL, FACE_BOOLEAN, FACE_NUMBER, FACE_NUMBER_FLOAT, FACE_TYPE,
	FACE_TYPE_BUILTIN, FACE_TYPE_DEFINITION, FACE_ATTRIBUTE, FACE_ATTRIBUTE_BUILTIN,
	FACE_PROPERTY, FACE_FUNCTION, FACE_FUNCTION_BUILTIN, FACE_FUNCTION_CALL,
	FACE_FUNCTION_MACRO, FACE_FUNCTION_METHOD, FACE_FUNCTION_METHOD_CALL, FACE_CONSTRUCTOR,
	FACE_OPERATOR, FACE_KEYWORD, FACE_KEYWORD_COROUTINE, FACE_KEYWORD_FUNCTION,
	FACE_KEYWORD_OPERATOR, FACE_KEYWORD_IMPORT, FACE_KEYWORD_TYPE, FACE_KEYWORD_MODIFIER,
	FACE_KEYWORD_REPEAT, FACE_KEYWORD_RETURN, FACE_KEYWORD_DEBUG, FACE_KEYWORD_EXCEPTION,
	FACE_KEYWORD_CONDITIONAL, FACE_KEYWORD_CONDITIONAL_TERNARY, FACE_KEYWORD_DIRECTIVE,
	FACE_KEYWORD_DIRECTIVE_DEFINE, FACE_PUNCTUATION_DELIMITER, FACE_PUNCTUATION_BRACKET,
	FACE_PUNCTUATION_SPECIAL, FACE_COMMENT, FACE_COMMENT_DOCUMENTATION, FACE_COMMENT_ERROR,
	FACE_COMMENT_WARNING, FACE_COMMENT_TODO, FACE_COMMENT_NOTE, FACE_MARKUP_STRONG,
	FACE_MARKUP_ITALIC, FACE_MARKUP_STRIKETHROUGH, FACE_MARKUP_UNDERLINE,
	FACE_MARKUP_HEADING, FACE_MARKUP_HEADING_1, FACE_MARKUP_HEADING_2,
	FACE_MARKUP_HEADING_3, FACE_MARKUP_HEADING_4, FACE_MARKUP_HEADING_5,
	FACE_MARKUP_HEADING_6, FACE_MARKUP_QUOTE, FACE_MARKUP_MATH, FACE_MARKUP_LINK,
	FACE_MARKUP_LINK_LABEL, FACE_MARKUP_LINK_URL, FACE_MARKUP_RAW, FACE_MARKUP_RAW_BLOCK,
	FACE_MARKUP_LIST, FACE_MARKUP_LIST_CHECKED, FACE_MARKUP_LIST_UNCHECKED, FACE_DIFF_PLUS,
	FACE_DIFF_MINUS, FACE_DIFF_DELTA, FACE_TAG, FACE_TAG_BUILTIN, FACE_TAG_ATTRIBUTE,
	FACE_TAG_DELIMITER,
	/* VS Code's entity.name.function under Catppuccin Mocha (a definition's
	   name: Lean's lexer, package 12): blue, italic */
	FACE_FUNCTION_DEFINITION,
	/* Org's faces where no face above looks the same (package 16):
	   org-level-7 and 8, org-todo, org-priority, org-tag, org-date,
	   org-table, org-formula, org-drawer, org-checkbox, org-document-info,
	   org-block, org-block-begin-line */
	FACE_ORG_LEVEL_7, FACE_ORG_LEVEL_8, FACE_ORG_TODO, FACE_ORG_PRIORITY, FACE_ORG_TAG,
	FACE_ORG_DATE, FACE_ORG_TABLE, FACE_ORG_FORMULA, FACE_ORG_DRAWER, FACE_ORG_CHECKBOX,
	FACE_ORG_INFO, FACE_ORG_BLOCK, FACE_ORG_BLOCK_LINE,
	/* render-markdown's heading backgrounds (RenderMarkdownH1Bg to H6Bg):
	   only ever a cell's bg */
	FACE_HEADING_1_BG, FACE_HEADING_2_BG, FACE_HEADING_3_BG, FACE_HEADING_4_BG,
	FACE_HEADING_5_BG, FACE_HEADING_6_BG,
	/* render-markdown's code background (RenderMarkdownCode, only ever a
	   cell's bg), its fence fill (bg_as_fg of it) and its bullets
	   (RenderMarkdownBullet) */
	FACE_MD_CODE_BG, FACE_MD_CODE_BORDER, FACE_MD_BULLET,
	/* Neovim's UI highlight groups, as Davy's catppuccin draws them:
	   CursorLine (only ever a cell's bg), CursorLineNr, LineNr, StatusLine,
	   StatusLineNC, WinSeparator, EndOfBuffer, NonText ('@@@', the '<' and
	   '>' fillers, and SpecialKey's ^X, which catppuccin links to it),
	   Visual, Search, CurSearch, IncSearch (also on_yank's flash),
	   MatchParen, Pmenu, PmenuSel, PmenuSbar, PmenuThumb, NormalFloat,
	   FloatBorder, FloatTitle, Cursor */
	FACE_CURSOR_LINE, FACE_CURSOR_LINE_NR, FACE_LINE_NR, FACE_STATUS_LINE,
	FACE_STATUS_LINE_NC, FACE_WIN_SEPARATOR, FACE_END_OF_BUFFER, FACE_NON_TEXT,
	FACE_VISUAL, FACE_SEARCH, FACE_CUR_SEARCH, FACE_INC_SEARCH, FACE_MATCH_PAREN,
	FACE_PMENU, FACE_PMENU_SEL, FACE_PMENU_SBAR, FACE_PMENU_THUMB,
	FACE_NORMAL_FLOAT, FACE_FLOAT_BORDER, FACE_FLOAT_TITLE, FACE_CURSOR,
	/* the status line (lualine, catppuccin theme): the mode pill
	   (lualine_a_<mode>, other being the terminal), the mode colour on
	   mantle (lualine_b_<mode>'s fg and the pill's rounded ends), and the b
	   section (surface0) */
	FACE_MODE_NORMAL, FACE_MODE_INSERT, FACE_MODE_VISUAL, FACE_MODE_REPLACE,
	FACE_MODE_COMMAND, FACE_MODE_OTHER, FACE_MODE_NORMAL_FG, FACE_MODE_INSERT_FG,
	FACE_MODE_VISUAL_FG, FACE_MODE_REPLACE_FG, FACE_MODE_COMMAND_FG, FACE_STATUS_INFO,
	/* signs (gitsigns) and diagnostics: Diagnostic{Error,Warn,Info,Hint}
	   for the sign, DiagnosticUnderline* for the span, DiagnosticVirtualText*
	   for the message after the line */
	FACE_GIT_ADD, FACE_GIT_CHANGE, FACE_GIT_DELETE, FACE_DIAG_ERROR, FACE_DIAG_WARN,
	FACE_DIAG_INFO, FACE_DIAG_HINT, FACE_DIAG_UNDER_ERROR, FACE_DIAG_UNDER_WARN,
	FACE_DIAG_UNDER_INFO, FACE_DIAG_UNDER_HINT, FACE_DIAG_VT_ERROR, FACE_DIAG_VT_WARN,
	FACE_DIAG_VT_INFO, FACE_DIAG_VT_HINT,
	/* indent guides (ATTR_GUIDE): every level, and the cursor's scope */
	FACE_INDENT, FACE_INDENT_SCOPE,
	/* the picker's match and prompt (snacks.picker), which-key's panel */
	FACE_PICK_MATCH, FACE_PICK_PROMPT, FACE_WK_GROUP, FACE_WK_DESC, FACE_WK_SEPARATOR,
	/* the save kitten (my-nvim's kitten.lua): cat, face, paw, heart, text, dim */
	FACE_KITTEN_CAT, FACE_KITTEN_FACE, FACE_KITTEN_PAW, FACE_KITTEN_HEART, FACE_KITTEN_TEXT,
	FACE_KITTEN_DIM,
	/* the start screen, in the app icon's style */
	FACE_DASHBOARD_TITLE, FACE_DASHBOARD_FOOTER, FACE_DASHBOARD_CAT, FACE_DASHBOARD_CAT_PATCH,
	FACE_DASHBOARD_FROG, FACE_DASHBOARD_FROG_CHEEK, FACE_DASHBOARD_FROG_MOUTH, FACE_DASHBOARD_SPARKLE,
	FACE_DASHBOARD_TERMINAL, FACE_DASHBOARD_PROMPT,
	/* splash.lua's shimmer, bold: the two of its colours no face above has
	   (pink, mauve, lavender, blue and green are kitten text, dashboard
	   prompt, title, footer and frog) */
	FACE_SHIMMER_SAPPHIRE, FACE_SHIMMER_TEAL,
	FACE_COUNT
};
/* The theme, once for every backend: a face's colours (FACE_FG, FACE_BG
   say which are set; otherwise the terminal's or window's default) and
   style.  ref/theme.c holds Catppuccin Mocha; no package replaces it.
   The slots from FACE_COUNT on are free, so every cell's face is one: the
   terminal (src/tools/term.c) sets them at run time to the colours its
   programs ask for (Neovim's attr table, get_attr_entry()).  1024 slots:
   btop, the most colourful program measured, needs at most 179 over two
   screens (measured, 5 runs, PR of the terminal thread); a uint16_t face
   costs no measurable diff time (measured, 7 alternating runs).
   sp is Neovim's special colour, for an underline or undercurl when
   FACE_SP is set (else the fg, as Neovim). */
enum { FACE_BOLD = 1, FACE_ITALIC = 2, FACE_UNDERLINE = 4, FACE_STRIKE = 8, FACE_FG = 16, FACE_BG = 32,
       FACE_UNDERCURL = 64, FACE_SP = 128 };
typedef struct { unsigned char fg[3], bg[3], style, sp[3]; } Face;
enum { FACES = 1024 };
extern Face faces[FACES];

/* A cell holds one grapheme cluster of at most GRID_CLUSTER_MAX bytes:
   Neovim's MAX_SCHAR_SIZE (32) less its NUL.  A longer cluster is cut to
   its longest prefix of whole code points that fits, and the rest is
   dropped, as Neovim's utfc_ptrlen2schar() does. */
enum { GRID_CLUSTER_MAX = 31 };

typedef struct {
	unsigned char ch[4], len;  /* the cluster's UTF-8, len bytes: up to 4 in
	                              ch, a longer one in the grid's cluster store
	                              and ch then holds its number (Neovim's
	                              schar_T); len 0 = right half of a wide one */
	unsigned char attr;
	unsigned short face;       /* FACE_* or a run-time slot below FACES,
	                              set by the core after grid_put; with it a
	                              Cell is 10 bytes */
	unsigned char bg;          /* 0, or a face whose background replaces the
	                              face's own: a background under a whole line
	                              (the cursor line, Neovim's extmark hl_eol), set by the core
	                              after grid_put, which clears it.  It makes
	                              a Cell 8 bytes (was 7): one face per cell
	                              cannot put a heading's bar under the
	                              other faces in the heading */
	unsigned char ul;          /* 0, or a face whose underline (FACE_UNDERLINE
	                              or FACE_UNDERCURL) and sp replace the face's
	                              own under the cell's text: a diagnostic's
	                              undercurl over a token's colour (Neovim
	                              combines an extmark's underline over
	                              syntax), set by the core after grid_put,
	                              which clears it.  the Cell stays 10 bytes (it fills
	                              face's padding) */
} Cell;

typedef struct {
	int rows, cols;
	Cell *c;              /* rows * cols */
	int crow, ccol;       /* the cursor */
	int cshape;           /* CURSOR_*: Neovim's default guicursor
	                         n-v-c-sm:block,i-ci-ve:ver25,r-cr-o:hor20 */
} Grid;

enum { CURSOR_BLOCK, CURSOR_BAR, CURSOR_UNDER };   /* bar: 25% of the cell's width; under: 20% of its height */

int  grid_resize(Grid *g, int rows, int cols);    /* 0 ok, -1 out of memory; cells cleared */
void grid_free(Grid *g);
void grid_clear(Grid *g);
/* Puts one cluster at (r, c) if it fits; returns the cells it took. */
int  grid_put(Grid *g, int r, int c, const unsigned char *s, size_t n, int width, int attr);
/* The bytes of a cell, len of them. */
const unsigned char *grid_text(const Cell *c);
/* Before each frame: when the cluster store is over half full it is
   emptied, the numbers in every grid go stale, and this returns 1, so the
   caller draws everything again (Neovim's schar_cache_clear_if_full()).
   The store is static: nothing is allocated. */
int  grid_frame(void);
/* Puts UTF-8 s (n bytes) from column c, cluster by cluster at uni_width's
   cells, up to column end (a cluster that would pass it is left out): what
   is not printable (a control character, an invalid byte, <xxxx>) as that
   many blanks, so the layout matches grid_width.  Every cell gets attr and
   face.  Returns the next column. */
int  grid_puts(Grid *g, int r, int c, int end, const char *s, size_t n, int attr, int face);
/* The cells grid_puts takes for s (Neovim's mb_string2cells()). */
int  grid_width(const char *s, size_t n);
int  grid_cell_eq(const Cell *a, const Cell *b);
/* The face cell x is drawn with (FACE_NONE for an unknown one); in *bg the
   face its background comes from and in *ul the face its underline style and
   sp come from: each layer if set, else the same. */
const Face *grid_face(const Cell *x, const Face **bg, const Face **ul);
/* An undercurl's height above a cell's last pixel row at pixel column x, in
   a cell h pixels tall: a triangle wave (kitty's undercurl), for the
   backends that draw pixels. */
int  grid_curl(int x, int h);
#endif
