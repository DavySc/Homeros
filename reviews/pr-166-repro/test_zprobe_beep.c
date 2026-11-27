/* Re-review of PR 166 at 4f22848b: what the beep for SPC 5 {no frame's key}
   leaves behind.  At the head: the count is dropped and nothing is pending.
   With `lead_end(e);` taken out of that branch (plants-mine-2.txt, not
   caught by test_keys_map) the count survives: 2 SPC 5 z x deletes 2. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "core.h"
#include "keys.h"

static void feed(Editor *e, const char *s)
{
	for (; *s; s++) keys_input(e, (unsigned char)*s, s[1] != 0);
}

int main(void)
{
	char path[] = "/tmp/zprobeXXXXXX";
	int fd = mkstemp(path);
	if (fd < 0 || write(fd, "abcdefghij\n", 11) != 11 || close(fd)) return 99;
	static Editor e;
	if (core_open(&e, path)) return 99;
	setvbuf(stdout, NULL, _IONBF, 0);
	feed(&e, "2 5z");
	printf("after 2 SPC 5 z: pending %d\n", e.ks.pending);
	feed(&e, "x");
	size_t n = text_len(e.buf->text);
	printf("then x: %zu bytes left (9 = one deleted, the count dropped; 8 = the count kept)\n", n - 1);
	core_free(&e);
	unlink(path);
	return n != 10;
}
