/* libFuzzer: any key sequence leaves the editor consistent (the cursor on
   the first byte of a character, the text valid UTF-8), and undoing
   everything gives back the loaded text exactly (Undo.lean's undo_group law
   on the whole editor), redoing everything the text it had.  As in
   fuzz_keys_all.c, each input starts a fresh session, so a failing input
   replays alone, and a count is limited to one digit to bound the work:
   22222222O only measures the rope (test_text, fuzz_text) and passes the
   64 KiB read buffer.  Multi-digit counts are checked by conform.py's
   random sequences (10 to 19, typed digit by digit) and by the recorded
   traces' long counts (count folding, 999999999). */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "core.h"
#include "uni.h"
#include "../src/keys/k.h"

static char path[64];

static void read_all(Editor *e, char *b, size_t *n) { *n = text_read(e->buf->text, 0, b, 1 << 16); }

/* every key inserts valid UTF-8, so no edit may split a character */
static void valid(const char *b, size_t n)
{
	for (size_t i = 0, k; i < n; i += k)
		if ((k = uni_len((const unsigned char *)b, n, i)) == 1 && (unsigned char)b[i] >= 0x80) abort();
}

int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n)
{
	static const int keys[] = { 'h', 'j', 'k', 'l', '0', '^', '$', 'w', 'b', 'g', 'G', 'x', 'd', 'i', 'a',
		'I', 'A', 'o', 'O', 'u', KEY_CTRL_R, KEY_ESC, KEY_ENTER, KEY_BS, KEY_TAB, ' ', 'q', 0xE9, '2', KEY_LEFT, 0x3BB, 0x10D0, 0x10400, KEY_DEL };
	static char before[1 << 16], after[1 << 16], now[1 << 16];
	static Editor e;
	size_t nb, na, nn;
	if (!path[0]) {
		strcpy(path, "/tmp/homeros-fuzz-XXXXXX");
		int fd = mkstemp(path);
		if (fd < 0 || write(fd, "ab \xce\xbb" "d \xe1\x83\x90\xf0\x90\x90\x80" "\n\tef\n\n  gh", 24) != 24) abort();
		close(fd);
	}
	normal_reset();
	ops_reset();
	edit_reset();
	motion_reset();
	if (core_open(&e, path)) abort();
	read_all(&e, before, &nb);
	int prev = 0;
	for (size_t i = 0; i < n && text_len(e.buf->text) < 30000; i++) {
		int k = keys[d[i] % (sizeof keys / sizeof *keys)];
		if (k >= '0' && k <= '9' && prev >= '0' && prev <= '9') continue;
		prev = k;
		keys_feed(&e, k, i + 1 < n);
		size_t len, nl = core_lines(&e);
		const unsigned char *s = core_line(&e, e.win->line, &len);
		if (!nl || e.win->line >= nl || e.win->col > len) abort();
		size_t p = 0;   /* the cursor is where a scan from the line start lands */
		while (p < e.win->col) p += uni_len(s, len, p);
		if (p != e.win->col) abort();
		if (e.mode == MODE_NORMAL && len && e.win->col >= len) abort();
		if (text_len(e.buf->text) == 0 || core_line_start(&e, nl) != text_len(e.buf->text)) abort();
	}
	keys_feed(&e, KEY_ESC, 0);
	if (e.mode != MODE_NORMAL) keys_feed(&e, KEY_ESC, 0);
	read_all(&e, after, &na);
	valid(after, na);
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
