/* Vim keys: the grammar from keys to core calls.  ref/keys.c knows the thin
   editor's set (h j k l 0 ^ $ w b gg G x dd i a I A o O u CTRL-R, counts,
   insert-mode text, Enter, Backspace, Tab, :w :w! :q :q! :wq :x).
   Package 5 (src/keys/) grows it to FEATURES.md, checked against
   nvim --headless (tools/conform.py). */
#ifndef KEYS_H
#define KEYS_H

/* A key is a Unicode code point (control keys as their ASCII code, so Esc is
   27, CTRL-R is 18) or one of these. */
enum {
	KEY_UP = 0x110000, KEY_DOWN, KEY_LEFT, KEY_RIGHT,
	KEY_RESIZE,
	KEY_CTRL_C = 3, KEY_TAB = 9, KEY_ENTER = 13, KEY_CTRL_R = 18,
	KEY_CTRL_Z = 26, KEY_ESC = 27, KEY_BS = 127
};

/* What keys.c remembers between keys of one command. */
typedef struct {
	unsigned long count;
	int op;     /* pending operator or prefix: 'd', 'g', or 0 */
	int space;  /* Neovim's inserted_space: a space typed on its own */
	int batch;  /* inserting typeahead as one string (Neovim's insertchar()) */
} KeyState;

struct Editor;
/* pending: more keys are already waiting (typeahead), which changes how
   Neovim inserts plain characters */
void keys_feed(struct Editor *e, int key, int pending);
#endif
