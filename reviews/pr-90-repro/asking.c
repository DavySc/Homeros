/* asking FILE OTHER CASE: the c flag's question open (:%s/o/XY/gc, one y),
   then something that is not a key, then the answer y.
   a: the text shrinks under it (core_edit, as a reload or a format does)
   b: another file comes into the window (core_edit_file, as a picker does)
   c: the window closes (KEY_CLOSE) with another window on another file */
#include <stdio.h>
#include <string.h>
#include "core.h"
#include "keys.h"
static Editor e;
static void feed(const char *s) { for (; *s; s++) keys_feed(&e, *s == '\r' ? KEY_ENTER : *s, s[1] != 0); }
static void show(const char *what)
{
	printf("%s: buffer %s, pending %d, msg [%s]\n", what, e.buf->path ? e.buf->path : "?", e.ks.pending, e.msg);
	for (size_t l = 0; l < core_lines(&e) && l < 12; l++) { size_t n; const unsigned char *p = core_line(&e, l, &n); printf("  |%.*s|\n", (int)n, p); }
}
int main(int argc, char **argv)
{
	if (argc < 4 || core_open(&e, argv[1])) return 2;
	if (argv[3][0] == 'c') { core_split(&e, 0, 0); core_edit_file(&e, argv[2], 0); feed("\x17w"); core_edit_file(&e, argv[1], 0); }
	feed("jj:%s/o/XY/gc\ry");
	show("asking");
	if (argv[3][0] == 'a') core_edit(&e, 3, text_len(e.buf->text) - 3, "", 0);
	if (argv[3][0] == 'b') core_edit_file(&e, argv[2], 1);
	if (argv[3][0] == 'c') keys_feed(&e, KEY_CLOSE, 0);
	show("after the event");
	feed("y");
	show("after y");
	feed("y");
	show("after y y");
	return 0;
}
