/* libFuzzer: any key sequence leaves the editor consistent, and undoing
   everything gives back the loaded text exactly (Undo.lean's undo_group law
   on the whole editor), redoing everything the text it had. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "core.h"

static char path[64];

static void read_all(Editor *e, char *b, size_t *n) { *n = text_read(e->text, 0, b, 1 << 16); }

int LLVMFuzzerTestOneInput(const uint8_t *d, size_t n)
{
	static const int keys[] = { 'h', 'j', 'k', 'l', '0', '^', '$', 'w', 'b', 'g', 'G', 'x', 'd', 'i', 'a',
		'I', 'A', 'o', 'O', 'u', KEY_CTRL_R, KEY_ESC, KEY_ENTER, KEY_BS, KEY_TAB, ' ', 'q', 0xE9, '2', KEY_LEFT };
	static char before[1 << 16], after[1 << 16], now[1 << 16];
	static Editor e;
	size_t nb, na, nn;
	if (!path[0]) {
		strcpy(path, "/tmp/homeros-fuzz-XXXXXX");
		int fd = mkstemp(path);
		if (fd < 0 || write(fd, "ab cd\n\tef\n\n  gh", 15) != 15) abort();
		close(fd);
	}
	if (core_open(&e, path)) abort();
	read_all(&e, before, &nb);
	for (size_t i = 0; i < n && text_len(e.text) < 30000; i++) {
		keys_feed(&e, keys[d[i] % (sizeof keys / sizeof *keys)], i + 1 < n);
		size_t len, nl = core_lines(&e);
		core_line(&e, e.line, &len);
		if (!nl || e.line >= nl || e.col > len) abort();
		if (e.mode == MODE_NORMAL && len && e.col >= len) abort();
		if (text_len(e.text) == 0 || core_line_start(&e, nl) != text_len(e.text)) abort();
	}
	keys_feed(&e, KEY_ESC, 0);
	if (e.mode != MODE_NORMAL) keys_feed(&e, KEY_ESC, 0);
	read_all(&e, after, &na);
	unsigned long seq = undo_seq(e.undo);
	while (undo_seq(e.undo)) keys_feed(&e, 'u', 0);
	read_all(&e, now, &nn);
	if (nn != nb || memcmp(now, before, nb)) abort();
	while (undo_seq(e.undo) != seq) {
		unsigned long s = undo_seq(e.undo);
		keys_feed(&e, KEY_CTRL_R, 0);
		if (undo_seq(e.undo) == s) abort();
	}
	read_all(&e, now, &nn);
	if (nn != na || memcmp(now, after, na)) abort();
	core_free(&e);
	return 0;
}
