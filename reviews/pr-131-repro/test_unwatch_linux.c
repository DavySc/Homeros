/* Review of PR 131: on Linux, a buffer left open still sees an outside
   change through the loop after :bd of another file, as test_git_win's
   item 4 checks on Windows.  Passes on the head; fails when file.c's
   file_unwatch returns 1 (the core then takes the one shared fd out of the
   loop), a plant no Linux test catches.
   Built as a test: cp to tests/test_unwatch_linux.c, make test TESTS=unwatch_linux */
#include <stdio.h>
#include <unistd.h>
#include "check.h"
#include "core.h"
#include "loop.h"

int main(void)
{
	char dir[] = "/tmp/homeros-unwatch-XXXXXX", a[64], b[64];
	CHECK(mkdtemp(dir));
	snprintf(a, sizeof a, "%s/a.txt", dir);
	snprintf(b, sizeof b, "%s/b.txt", dir);
	FILE *f;
	CHECK((f = fopen(a, "w")) && fputs("a\n", f) >= 0 && !fclose(f));
	CHECK((f = fopen(b, "w")) && fputs("b\n", f) >= 0 && !fclose(f));
	Editor e;
	CHECK(!core_open(&e, a) && !core_edit_file(&e, b, 0) && !core_buf_delete(&e, 1, 0));
	CHECK((f = fopen(b, "w")) && fputs("bb\n", f) >= 0 && !fclose(f));
	for (int i = 0; i < 40 && !e.buf->changed; i++) loop_once(50);
	CHECK(e.buf->changed);
	core_free(&e);
	unlink(a); unlink(b); rmdir(dir);
	puts("test_unwatch_linux: a buffer sees an outside change after :bd of another file");
	return 0;
}
