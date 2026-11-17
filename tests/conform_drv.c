/* Conformance driver: loads FILE, runs the keys given as numbers on stdin
   through the editor, prints the buffer's lines and the cursor as JSON for
   tools/conform.py to compare with nvim --headless. */
#include <stdio.h>
#include "core.h"

int main(int argc, char **argv)
{
	static Editor e;
	int k, next;
	if (argc != 2 || core_open(&e, argv[1])) return 2;
	/* nvim gets all keys at once through feedkeys(), so all are typeahead */
	for (int more = scanf("%d", &k) == 1; more; k = next) {
		more = scanf("%d", &next) == 1;
		keys_feed(&e, k, more);
	}
	/* feedkeys(…, 'x') ends an insert left open, or a command half typed:
	   vgetorpeek() returns ESC when asked for a key with the typeahead empty
	   ("Must return something to avoid getting stuck"); a pending CTRL-V or
	   CTRL-R eats one */
	while (e.mode == MODE_INSERT || e.ks.pending) keys_feed(&e, KEY_ESC, 0);
	printf("{\"lines\": [");
	for (size_t l = 0; l < core_lines(&e); l++) {
		size_t n;
		const unsigned char *s = core_line(&e, l, &n);
		printf(l ? ", \"" : "\"");
		for (size_t i = 0; i < n; i++)
			if (s[i] == '"' || s[i] == '\\') printf("\\%c", s[i]);
			else if (s[i] < 0x20) printf("\\u%04x", s[i]);
			else putchar(s[i]);
		putchar('"');
	}
	printf("], \"cursor\": [%zu, %zu], \"mode\": %d}\n", e.win->line + 1, e.win->col, e.mode);
	core_free(&e);
	return 0;
}
