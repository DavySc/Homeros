/* P4: v selection ending on a multi-byte character; F5 in insert mode; :bd; results buffer state */
#include "h.h"
static void show(const char *what) { printf("--- %s: mode=%d wins=%d msg=[%s]\n%s\n", what, e.mode, nwins(), e.msg, results()); }
int main(void)
{
	setup();
	put("ms/a.sql", "select N'caf\xc3\xa9' as x, 2 as y\nselect 'in insert' as z\n", 0600);
	core_open(&e, "ms/a.sql");
	/* v from the line's start to the é (the cursor on it), then F5 */
	keys("0vf"); keys_input(&e, 0xE9, 0);
	printf("cursor col %zu visual=%c\n", e.win->col, e.ks.visual);
	/* add the closing quote by hand is not possible in a selection: select up to é then expect an unclosed string error naming é intact */
	keys_input(&e, KEY_F5, 0);
	until("\n\nCompletion time");
	show("v up to the e-acute");
	for (size_t i = 0; res[i]; i++) if ((unsigned char)res[i] >= 0x80) printf("byte %02x ", (unsigned char)res[i]);
	printf("\n");
	keys("\033");
	/* F5 while typing */
	keys("2GA");
	keys_input(&e, KEY_F5, 0);
	for (int i = 0; i < 50; i++) loop_once(10);
	char line[200] = {0};
	text_read(e.buf->text, text_line_start(e.buf->text, 1), line, 60);
	printf("--- F5 in insert mode: mode=%d line 2 now=[%s]\n", e.mode, line);
	keys("\033");
	/* the results buffer: modified? :q in it; then :qa */
	keys(":q\r");
	show("after :q in the query window");
	printf("quit=%d\n", e.quit);
	core_free(&e);
	return 0;
}
