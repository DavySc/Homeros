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
	KEY_CTRL_C = 3, KEY_TAB = 9, KEY_ENTER = 13, KEY_CTRL_R = 18,
	KEY_CTRL_Z = 26, KEY_ESC = 27, KEY_BS = 127
};

/* The key state the core draws (one per editor: Neovim has one typeahead).  Visual mode is
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
	int pending;         /* a command is half typed: a count, register, operator or its
	                        character (what showcmd shows); the conform driver ends it
	                        with Esc, as feedkeys(..., 'x') does */
} KeyState;

struct Editor;
/* pending: more keys are already waiting (typeahead), which changes how
   Neovim inserts plain characters */
void keys_feed(struct Editor *e, int key, int pending);
/* Davy's Normal-mode maps (FEATURES.md 4: Space as leader, - for oil), for
   main.c: 1 if it took the key; while a map is incomplete ks.pending is set,
   so the next key comes here first.  0: feed it to keys_feed */
int keys_map(struct Editor *e, int key);
#endif
