/* The journal through the core: random keys with saves, then a crash (the
   journal's bytes kept, the editor dropped), then a reopen, 500 times: the
   reopened buffer has the text and flags the crashed one had, and noeol
   too unless the buffer is empty (an empty buffer saves 0 bytes either way);
   it is marked modified exactly when the text differs from the file.  Also:
   an outside change gives W11 at the next edit, E13 at a plain :w, and a
   recovered buffer whose file changed since keeps the old identity, so :w
   is refused there too. */
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "check.h"
#include "core.h"

static char dir[] = "/tmp/homeros-core-XXXXXX", path[256], jname[700];

static void put(const char *p, const char *s)
{
	int fd = open(p, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	CHECK(fd >= 0 && write(fd, s, strlen(s)) == (ssize_t)strlen(s));
	close(fd);
}

static size_t slurp(const char *p, char *b, size_t n)
{
	int fd = open(p, O_RDONLY);
	if (fd < 0) return 0;
	ssize_t r = read(fd, b, n);
	close(fd);
	return r < 0 ? 0 : (size_t)r;
}

/* drops the editor as a crash would: the clean close removes the journal,
   so its bytes are put back */
static void crash(Editor *e)
{
	static char j[1 << 20];
	size_t n = slurp(jname, j, sizeof j);
	core_free(e);
	int fd = open(jname, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	CHECK(fd >= 0 && write(fd, j, n) == (ssize_t)n);
	close(fd);
}

static void sessions(void)
{
	static const int keys[] = { 'h', 'j', 'k', 'l', '0', '$', 'w', 'b', 'G', 'x', 'd', 'i', 'a', 'I', 'A',
		'o', 'O', 'u', KEY_CTRL_R, KEY_ESC, KEY_ENTER, KEY_BS, KEY_TAB, ' ', 'q', 0xE9, '2', -1 };
	static const char *texts[] = { "ab \xce\xbb" "d\n\tef\n\n  gh", "", "x\n", "no eol" };
	static char a[1 << 16], b[1 << 16], f[1 << 16];
	int recovered = 0;
	for (int s = 0; s < 500; s++) {
		Editor e, g;
		put(path, texts[rnd(4)]);
		CHECK(!core_open(&e, path));
		for (int i = 0, n = (int)rnd(40); i < n; i++) {
			int k = keys[rnd(sizeof keys / sizeof *keys)];
			if (k < 0) { if (e.mode == MODE_NORMAL) core_write(&e, e.buf, 0); }
			else keys_feed(&e, k, i + 1 < n);
		}
		size_t na = text_read(e.buf->text, 0, a, sizeof a);
		unsigned flags = e.buf->flags;
		int noeol = e.buf->noeol;
		crash(&e);
		CHECK(!core_open(&g, path));
		size_t nb = text_read(g.buf->text, 0, b, sizeof b), nf = slurp(path, f, sizeof f);
		CHECK(nb == na && !memcmp(a, b, na) && g.buf->flags == flags);
		CHECK((flags & CORE_EMPTY) || g.buf->noeol == noeol);
		/* modified exactly when it would save something else */
		size_t ns = flags & CORE_EMPTY ? 0 : nb - (g.buf->noeol ? 1 : 0);
		CHECK(core_modified(g.buf) == !(ns == nf && !memcmp(b, f, nf)));
		recovered += core_modified(g.buf);
		core_free(&g);
	}
	printf("  500 crashed sessions reopened exactly (%d with unsaved text recovered)\n", recovered);
}

static void outside(void)
{
	Editor e;
	put(path, "one\n");
	CHECK(!core_open(&e, path) && e.buf->watch >= 0);
	char other[300];
	snprintf(other, sizeof other, "%s/other", dir);
	put(other, "outside\n");
	CHECK(!rename(other, path));
	struct pollfd p = { e.buf->watch, POLLIN, 0 };
	CHECK(poll(&p, 1, 1000) == 1);
	core_watch(&e, e.buf->watch);
	keys_feed(&e, 'x', 0);
	CHECK(strstr(e.msg, "W11"));
	e.msg[0] = 0;
	keys_feed(&e, 'x', 0);
	CHECK(!strstr(e.msg, "W11"));                     /* once */
	CHECK(core_write(&e, e.buf, 0) == -1 && strstr(e.msg, "E13"));
	/* crashed now; the file changes again before the reopen */
	crash(&e);
	put(path, "again\n");
	CHECK(!core_open(&e, path) && core_modified(e.buf) && strstr(e.msg, "changed since"));
	CHECK(core_write(&e, e.buf, 0) == -1 && strstr(e.msg, "E13"));
	char b[64];
	CHECK(slurp(path, b, sizeof b) == 6 && !memcmp(b, "again\n", 6));
	CHECK(!core_write(&e, e.buf, 1));                 /* :w! */
	core_free(&e);
	CHECK(access(jname, F_OK));                         /* a clean close leaves no journal */
	puts("  outside change: W11 once at the next edit, E13 at :w, kept across a crash; :w! writes");
}

int main(void)
{
	CHECK(mkdtemp(dir));
	snprintf(path, sizeof path, "%s/f", dir);
	char st[300];
	snprintf(st, sizeof st, "%s/state", dir);
	CHECK(!setenv("XDG_STATE_HOME", st, 1));
	snprintf(jname, sizeof jname, "%s/homeros/journal/%s", st, path);
	for (char *c = jname + strlen(st) + 17; *c; c++) if (*c == '/') *c = '%';
	sessions();
	outside();
	char cmd[300];
	snprintf(cmd, sizeof cmd, "rm -rf %s", dir);
	CHECK(!system(cmd));
	puts("test_file_core: crashed sessions recovered through the core, outside changes warned and refused");
	return 0;
}
