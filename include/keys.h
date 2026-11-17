/* Vim keys: the grammar from keys to core calls.  ref/keys.c knows the thin
   editor's set (h j k l 0 ^ $ w b gg G x dd i a I A o O u CTRL-R, counts,
   insert-mode text, Enter, Backspace, Tab, :w :w! :q :q! :wq :x).
   Package 5 (src/keys/) grows it to FEATURES.md, checked against
   nvim --headless (tools/conform.py). */
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
	KEY_CTRL_C = 3, KEY_TAB = 9, KEY_ENTER = 13, KEY_CTRL_R = 18,
	KEY_CTRL_Z = 26, KEY_ESC = 27, KEY_BS = 127
};

/* The key state the core draws or keeps per window.  Visual mode is
   MODE_NORMAL with a selection and replace mode is MODE_INSERT that
   overtypes, so the core's modes and clamping stay as they are.  The pending
   command, count, registers, dot repeat and macros are global in Neovim (one
   typeahead, one register set), so they stay inside keys.c. */
typedef struct {
	int visual;          /* 0, 'v', 'V' or 22 (CTRL-V); the cursor is one end */
	size_t vline, vcol;  /* the selection's other end */
	int replace;         /* insert mode overtypes (R) */
	int space;           /* Neovim's inserted_space: a space typed on its own */
	int batch;           /* inserting typeahead as one string (Neovim's insertchar()) */
} KeyState;

struct Editor;
/* pending: more keys are already waiting (typeahead), which changes how
   Neovim inserts plain characters */
void keys_feed(struct Editor *e, int key, int pending);
#endif
