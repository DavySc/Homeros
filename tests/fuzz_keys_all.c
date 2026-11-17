/* libFuzzer over every key package 5 handles (fuzz_keys.c keeps the thin
   editor's dozen): the same laws, any sequence leaves the cursor on a
   character's first byte, undoing everything gives back the loaded text and
   redoing everything the text it had.  Not that the text stays valid UTF-8:
   a recorded <Down> is Neovim's K_SPECIAL bytes 80 6B 64, which a put
   inserts as they are (measured: "qa<Down>q" then "ap gives x<80>kd).  Not @ and Q: a
   macro that runs itself loops in Neovim too, until CTRL-C, which this
   loop cannot send.  Registers, the redo buffer and the last insert carry
   over from one input to the next, as from one command to the next, so a
   failure may need the inputs before it (run with a log of them to replay). */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "core.h"
#include "uni.h"
#include "../src/keys/k.h"

static char path[64];

static void read_all(Editor *e, char *b, size_t *n) { *n = text_read(e->buf->text, 0, b, 1 << 16); }

int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n)
{
	static const int keys[] = {
		'h', 'j', 'k', 'l', '0', '^', '$', 'w', 'b', 'e', 'W', 'B', 'E', 'g', 'G', 'f', 'F', 't', 'T', ';', ',',
		'|', '_', '+', '-', '(', ')', '{', '}', '[', ']', 'x', 'X', 'd', 'D', 'c', 'C', 's', 'S', 'y', 'Y',
		'p', 'P', 'J', 'u', 'U', '~', 'r', 'R', 'i', 'I', 'a', 'A', 'o', 'O', 'v', 'V', '.', '<', '>', '"',
		'\'', '`', 'm', 'q', 'z', 'H', 'M', 'L', 'n', 'p', '1', '2', '3', '_', '-', ' ', '(', '"', '\'', '<',
		KEY_ESC, KEY_ESC, KEY_ENTER, KEY_BS, KEY_TAB, KEY_CTRL_R, 22, 1, 24, 15, 23, 20, 4, 21, 5, 25,
		KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_DEL, 0xE9, 0x3BB, 0x10D0, 0x10400, 0x2200, 0x1D4DD, 0x1E9E,
	};
	static char before[1 << 16], after[1 << 16], now[1 << 16];
	static Editor e;
	size_t nb, na, nn;
	if (!path[0]) {
		strcpy(path, "/tmp/homeros-fuzz-XXXXXX");
		int fd = mkstemp(path);
		static const char t[] = "ab (\xce\xbb" "d) \xe1\x83\x90\xf0\x90\x90\x80 \"x y\"\n\tef {\n\n  gh \xe2\x88\x80 [i]\n";
		if (fd < 0 || write(fd, t, sizeof t - 1) != (ssize_t)(sizeof t - 1)) abort();
		close(fd);
	}
	if (core_open(&e, path)) abort();
	read_all(&e, before, &nb);
	for (size_t i = 0; i < n && text_len(e.buf->text) < 30000; i++) {
		keys_feed(&e, keys[d[i] % (sizeof keys / sizeof *keys)], i + 1 < n);
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
	/* end whatever is open: an insert, Visual mode, an operator, a recording */
	for (int i = 0; i < 4; i++) keys_feed(&e, KEY_ESC, 0);
	if (reg_recording) keys_feed(&e, 'q', 0);
	if (e.mode != MODE_NORMAL || VIsual_active || reg_recording) abort();
	read_all(&e, after, &na);
	unsigned long seq = undo_seq(e.buf->undo);
	while (undo_seq(e.buf->undo)) keys_feed(&e, 'u', 0);
	read_all(&e, now, &nn);
	if (nn != nb || memcmp(now, before, nb)) abort();
	while (undo_seq(e.buf->undo) != seq) {
		unsigned long s = undo_seq(e.buf->undo);
		keys_feed(&e, KEY_CTRL_R, 0);
		if (undo_seq(e.buf->undo) == s) abort();
	}
	read_all(&e, now, &nn);
	if (nn != na || memcmp(now, after, na)) abort();
	core_free(&e);
	return 0;
}
