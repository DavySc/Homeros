/* '[ and '] are the buffer's: a yank in one buffer must not set them in another */
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
	char a[] = "/tmp/twobufAXXXXXX", b[] = "/tmp/twobufBXXXXXX";
	int fa = mkstemp(a), fb = mkstemp(b);
	CHECK(write(fa, "a1\na2\na3\na4\n", 12) == 12 && !close(fa) && write(fb, "b1\nb2\nb3\nb4\n", 12) == 12 && !close(fb));
	CHECK(!core_open(&e, a));
	feed("jjyy");
	CHECK(!core_edit_file(&e, b, 0));
	e.msg[0] = 0;
	feed("`[");
	printf("in B after a yank in A: line %zu, msg [%s]\n", e.win->line + 1, e.msg);
	feed("ggx");            /* '[ in B is line 1 */
	CHECK(!core_edit_file(&e, a, 1));
	e.msg[0] = 0;
	feed("G`[");
	printf("back in A (its '[ is line 3): line %zu, msg [%s]\n", e.win->line + 1, e.msg);
	unlink(a), unlink(b);
	return 0;
}
