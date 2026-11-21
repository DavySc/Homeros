/* Vim keys: the grammar from keys to core calls, ported from Neovim 0.12 in
   src/keys/ (package 5) and checked against nvim --headless
   (tools/conform.py); src/keys/README lists what is not built yet. */
#ifndef KEYS_H
#define KEYS_H
#include <stddef.h>

/* A key is a Unicode code point (control keys as their ASCII code, so Esc is
   27, CTRL-R is 18) or one of these. */
enum {
	KEY_UP = 0x110000, KEY_DOWN, KEY_LEFT, KEY_RIGHT,
	KEY_RESIZE,
	KEY_MOUSE_LEFT, KEY_WHEEL_UP, KEY_WHEEL_DOWN,   /* at the cell Backend.mouse gives */
	KEY_CLOSE,           /* the window was asked to close (its X button, Alt-F4): :qa */
	KEY_DEL,             /* x in normal mode; in insert mode ins_del() */
	KEY_S_TAB,           /* Shift-Tab: a snippet's previous stop, else a Tab in insert mode */
	KEY_CTRL_C = 3, KEY_TAB = 9, KEY_ENTER = 13, KEY_CTRL_R = 18,
	KEY_CTRL_Z = 26, KEY_ESC = 27, KEY_BS = 127
};

/* The key state the core draws (one per editor: Neovim has one typeahead).  Visual mode is
   MODE_NORMAL with a selection and replace mode is MODE_INSERT that
   overtypes, so the core's modes and clamping stay as they are.  The pending
   command, count, registers, dot repeat and macros are global in Neovim (one
   typeahead, one register set), so they stay inside keys.c. */
/* An area of the buffer the core draws over its text: type 0 none, 'v'
   (l1,c1 to l2,c2 inclusive, byte columns on a character's first byte),
   'V' (lines l1 to l2) or 22 (a block: c1 to c2 are screen columns). */
typedef struct { int type; size_t l1, c1, l2, c2; } KeyArea;
typedef struct {
	int visual;          /* 0, 'v', 'V' or 22 (CTRL-V); the cursor is one end */
	size_t vline, vcol;  /* the selection's other end */
	int replace;         /* insert mode overtypes (R) */
	int executing;       /* a macro register is being replayed (reg_executing()) */
	int space;           /* Neovim's inserted_space: a space typed on its own */
	int batch;           /* inserting typeahead as one string (Neovim's insertchar()) */
	KeyArea yank;        /* the area just yanked, '[ to '] (a block's columns
	                        as getregionpos() gives them: the marks' leftmost
	                        virtcol and the register's width), flashed for
	                        150 ms (vim.hl.on_yank) in IncSearch */
	KeyArea inc;         /* the match :s with the c flag asks about (later
	                        'incsearch' while / or ? is typed), IncSearch;
	                        type 'v' while on */
	int pending;         /* a command is half typed: a count, register, operator or its
	                        character (what showcmd shows); the conform driver ends it
	                        with Esc, as feedkeys(..., 'x') does */
} KeyState;

struct Editor;
struct Win;
/* pending: more keys are already waiting (typeahead), which changes how
   Neovim inserts plain characters */
void keys_feed(struct Editor *e, int key, int pending);
/* A typed key, for main.c: an open picker (tools_key), git (git_key), then
   Davy's Normal-mode maps (FEATURES.md 4: Space as leader, - for oil), then
   keys_feed.  While a command or a map is half typed (ks.pending) the key
   goes to the map, if one is incomplete, or straight to keys_feed */
void keys_input(struct Editor *e, int key, int pending);
/* 'hlsearch' (FEATURES.md 1): the next match of the last search pattern
   that covers line `line` of window w at or after byte col there: 1 and
   its bytes [*s, *z) on this line (a match from an earlier line starts at
   0; one that takes the line break ends at the line's length + 1, its cell
   after the last character; an empty match draws one character, the line
   break's cell at the end), or 0 for none and while :noh or CTRL-L hides
   them until the next search.  The core calls it for each line of w it
   draws, from col 0 and then from *z (one character on after an empty
   match), and draws Search, and CurSearch for the match at the cursor.  It
   runs in the redraw path: after the pattern's compile, only the keys'
   line copies grow, to the longest line drawn, then stay
   (tests/test_keys_ex.c). */
int  keys_hl(struct Editor *e, const struct Win *w, size_t line, size_t col, size_t *s, size_t *z);
#endif
