/* Review probe for PR 106: tests/fuzz_keys_all.c, with a search pattern set
   first, / : and Enter among the keys, and a frame drawn after every key in
   two windows of different sizes, so the yank flash, ks.inc and 'hlsearch'
   are drawn over whatever text the next keys leave (the flash's timer never
   runs here: its area goes stale).
   Original: libFuzzer over every key package 5 handles (fuzz_keys.c keeps the thin
   editor's dozen): the same laws, any sequence leaves the cursor on a
   character's first byte, undoing everything gives back the loaded text and
   redoing everything the text it had.  Not that the text stays valid UTF-8:
   a recorded <Down> is Neovim's K_SPECIAL bytes 80 6B 64, which a put
   inserts as they are (measured: "qa<Down>q" then "ap gives x<80>kd).  Not @ and Q: a
   macro that runs itself loops in Neovim too, until CTRL-C, which this
   loop cannot send.  Each input starts a fresh session (registers, redo, last
   insert, last f/t), so a failing input replays alone.  A count is one
   digit (two with an operator's): typed counts reach 999999999, and
   333333o or 222222222p only measure the rope's speed or memory, which
   test_text and fuzz_text check. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "core.h"
#include "uni.h"
#include "../src/keys/k.h"
#include "../src/core/core_int.h"

/* the same text in a .txt and a .sql file, which gq formats through sqlfmt */
static char path[2][64];

static void read_all(Editor *e, char *b, size_t *n) { *n = text_read(e->buf->text, 0, b, 1 << 16); }

int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n)
{
	static const int keys[] = {
		'h', 'j', 'k', 'l', '0', '^', '$', 'w', 'b', 'e', 'W', 'B', 'E', 'g', 'G', 'f', 'F', 't', 'T', ';', ',',
		'|', '_', '+', '-', '(', ')', '{', '}', '[', ']', 'x', 'X', 'd', 'D', 'c', 'C', 's', 'S', 'y', 'Y',
		'p', 'P', 'J', 'u', 'U', '~', 'r', 'R', 'i', 'I', 'a', 'A', 'o', 'O', 'v', 'V', '.', '<', '>', '"',
		'\'', '`', 'm', 'q', 'z', 'H', 'M', 'L', 'n', 'p', '1', '2', '3', '_', '-', ' ', '(', '"', '\'', '<',
		KEY_ESC, KEY_ESC, KEY_ENTER, '/', '?', '*', '#', 'N', ':', 's', '/', 'a', '/', 'b', '/', 'c', KEY_ENTER, 'y', 'y', 22, '\\', 'n', '$', KEY_BS, KEY_TAB, KEY_CTRL_R, 22, 1, 24, 15, 23, 20, 4, 21, 5, 25,
		KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_DEL, 0xE9, 0x3BB, 0x10D0, 0x10400, 0x2200, 0x1D4DD, 0x1E9E,
	};
	static char before[1 << 16], after[1 << 16], now[1 << 16];
	static Editor e;
	size_t nb, na, nn;
	for (int i = 0; i < 2 && !path[i][0]; i++) {
		strcpy(path[i], i ? "/tmp/homeros-fuzz-XXXXXX.sql" : "/tmp/homeros-fuzz-XXXXXX.txt");
		int fd = mkstemps(path[i], 4);
		static const char t[] = "ab (\xce\xbb" "d) \xe1\x83\x90\xf0\x90\x90\x80 \"x y\"\n\tef {\n\n  gh \xe2\x88\x80 [i]\n";
		if (fd < 0 || write(fd, t, sizeof t - 1) != (ssize_t)(sizeof t - 1)) abort();
		close(fd);
	}
	normal_reset();
	ops_reset();
	edit_reset();
	motion_reset();
	if (core_open(&e, path[n && d[0] & 1])) abort();
	int nr = e.buf->nr;
	static Grid g;
	static const char *pre[] = { "/a\r", "/\\n\r", "/$\r", "/ *\r", "/b\\_.*e\r", "" };
	if (grid_resize(&g, n > 1 && d[1] & 1 ? 4 : 12, n > 1 && d[1] & 2 ? 9 : 40)) abort();
	layout_resize(&e, g.rows, g.cols);
	core_set_wrap(&e, n > 1 && d[1] & 4);
	for (const char *p = pre[n > 2 ? d[2] % 6 : 5]; *p; p++) keys_feed(&e, *p == '\r' ? KEY_ENTER : *p, 1);
	read_all(&e, before, &nb);
	int prev = 0, lit = 0;
	for (size_t i = 0; i < n && text_len(e.buf->text) < 30000; i++) {
		int k = keys[d[i] % (sizeof keys / sizeof *keys)];
		if (k >= '0' && k <= '9' && prev >= '0' && prev <= '9') continue;
		/* once CTRL-V may have put a literal CTRL-A in an insert, an Insert
		   mode CTRL-A can type itself again for ever, in Neovim too
		   (measured: "i s Left C-v C-a Left C-a" and "a Right C-v o C-a
		   Del C-a Right k B h C-a" had not ended in nvim --headless after
		   15 s) */
		if (k == 22 && e.mode == MODE_INSERT) lit = 1;
		if (k == 1 && lit && e.mode == MODE_INSERT) continue;
		prev = k;
		keys_feed(&e, k, i + 1 < n);
		core_update(&e);
		core_draw(&e, &g);
		size_t len, nl = core_lines(&e);
		const unsigned char *s = core_line(&e, e.win->line, &len);
		if (!nl || e.win->line >= nl || e.win->col > len) abort();
		size_t p = 0;   /* the cursor is where a scan from the line start lands */
		while (p < e.win->col) p += uni_len(s, len, p);
		if (p != e.win->col) abort();
		/* not col < len in Normal mode: after a Visual block operator Neovim
		   leaves the cursor on the NUL until the next command */
		if (text_len(e.buf->text) == 0 || core_line_start(&e, nl) != text_len(e.buf->text)) abort();
	}
	e.ks.yank.type = 0;
	core_free(&e);
	return 0;
}

