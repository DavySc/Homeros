/* The cell grid: what the core draws and a backend shows.  The core fills a
   grid each frame; a backend compares it with the last frame it showed and
   sends only changed cells (Neovim's grid, foot).  No allocation after
   grid_resize, so the redraw path allocates nothing. */
#ifndef GRID_H
#define GRID_H
#include <stddef.h>

enum { ATTR_NONE, ATTR_DIM, ATTR_REV };   /* line numbers, status line */
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
	FACE_TAG_DELIMITER, FACE_COUNT
};
/* The theme, once for every backend: a face's colours (FACE_FG, FACE_BG
   say which are set; otherwise the terminal's or window's default) and
   style.  ref/theme.c holds Catppuccin Mocha; no package replaces it. */
enum { FACE_BOLD = 1, FACE_ITALIC = 2, FACE_UNDERLINE = 4, FACE_STRIKE = 8, FACE_FG = 16, FACE_BG = 32 };
typedef struct { unsigned char fg[3], bg[3], style; } Face;
extern const Face faces[FACE_COUNT];

typedef struct {
	unsigned char ch[4], len;  /* one character, UTF-8; len 0 = right half of a wide one */
	unsigned char attr, face;  /* face: FACE_*, set by the core after grid_put */
} Cell;

typedef struct {
	int rows, cols;
	Cell *c;              /* rows * cols */
	int crow, ccol;       /* the cursor */
} Grid;

int  grid_resize(Grid *g, int rows, int cols);    /* 0 ok, -1 out of memory; cells cleared */
void grid_free(Grid *g);
void grid_clear(Grid *g);
/* Puts one character at (r, c) if it fits; returns the cells it took. */
int  grid_put(Grid *g, int r, int c, const unsigned char *s, size_t n, int width, int attr);
/* Puts an ASCII string; returns the next column. */
int  grid_puts(Grid *g, int r, int c, const char *s, int attr);
int  grid_cell_eq(const Cell *a, const Cell *b);
#endif
