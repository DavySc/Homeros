/* '[ and '] when a key itself changes the buffer (CTRL-W w to a window on another buffer) */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "check.h"
#include "core.h"
#include "keys.h"
static Editor e;
static void feed(const char *s) { for (; *s; s++) keys_feed(&e, *s == 27 ? KEY_ESC : (unsigned char)*s, s[1] != 0); }
int main(void)
{
	char a[] = "/tmp/twowinAXXXXXX", b[] = "/tmp/twowinBXXXXXX";
	int fa = mkstemp(a), fb = mkstemp(b);
	CHECK(write(fa, "a1\na2\na3\na4\n", 12) == 12 && !close(fa) && write(fb, "b1\nb2\nb3\nb4\n", 12) == 12 && !close(fb));
	CHECK(!core_open(&e, a));
	feed("\x17s");
	CHECK(!core_edit_file(&e, b, 0));
	feed("x");                    /* B's '[ is line 1 */
	feed("\x17w");
	printf("now in %s\n", strstr(e.buf->path, "twowinA") ? "A" : "B");
	feed("jjyy");                 /* A's '[ is line 3 */
	feed("\x17w");
	printf("now in %s\n", strstr(e.buf->path, "twowinA") ? "A" : "B");
	e.msg[0] = 0;
	feed("G`[");
	printf("in B: line %zu, msg [%s] (Neovim: line 1)\n", e.win->line + 1, e.msg);
	feed("\x17w");
	e.msg[0] = 0;
	feed("G`[");
	printf("in A: line %zu, msg [%s] (Neovim: line 3)\n", e.win->line + 1, e.msg);
	unlink(a), unlink(b);
	return 0;
}
