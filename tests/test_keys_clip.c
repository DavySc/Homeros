/* The system clipboard with 'clipboard' unnamedplus (Davy, 2026-11-18),
   through a fake backend: every yank and delete into the unnamed register,
   "+ or "* sets it, every put of those reads it (Neovim's set_clipboard()
   and get_clipboard()).  A text read back as it was set keeps its type;
   other text is lines when it ends in a line break.  Without a backend
   clipboard (NULL, the conform driver) the registers are Neovim's without
   a provider, which the recorded traces check.  src/keys/clip_oracle.lua
   types the same keys into Neovim with a fake provider (measured once,
   2026-11-18: the same text and clipboard after each of the 10 steps). */
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "check.h"
#include "core.h"
#include "keys.h"

static char clip[256];
static long clipn = -1;    /* -1: the clipboard cannot be read */
static int sets;

static int fake_set(const char *s, size_t n)
{
	CHECK(n < sizeof clip);
	memcpy(clip, s, n);
	clipn = (long)n;
	sets++;
	return 0;
}

/* returns the whole length, copying at most max bytes, as backend.h says */
static long fake_get(char *buf, size_t max)
{
	if (clipn > 0) memcpy(buf, clip, (size_t)clipn < max ? (size_t)clipn : max);
	return clipn;
}

static void keys(Editor *e, const char *s)
{
	for (; *s; s++) keys_feed(e, *s == 27 ? KEY_ESC : (unsigned char)*s, s[1] != 0);
}

static int is(Editor *e, const char *want)
{
	static char b[1024];
	size_t n = text_read(e->buf->text, 0, b, sizeof b - 1);
	b[n] = 0;
	if (strcmp(b, want)) fprintf(stderr, "text: [%s]\nwant: [%s]\n", b, want);
	return !strcmp(b, want);
}

static int has(const char *s) { return clipn == (long)strlen(s) && !memcmp(clip, s, (size_t)clipn); }

static void open_with(Editor *e, const char *path, const char *t)
{
	FILE *f = fopen(path, "w");
	CHECK(f && fputs(t, f) >= 0 && !fclose(f));
	CHECK(!core_open(e, path));
	e->clip_set = fake_set;
	e->clip_get = fake_get;
}

int main(void)
{
	char path[] = "/tmp/keys_clipXXXXXX";
	int fd = mkstemp(path);
	CHECK(fd >= 0 && !close(fd));
	static Editor e;

	/* yanks and deletes set it: lines end in a line break */
	open_with(&e, path, "one two\nthree\n");
	keys(&e, "yy");
	CHECK(has("one two\n"));
	keys(&e, "dw");                    /* the small delete register */
	CHECK(has("one ") && is(&e, "two\nthree\n"));
	/* other registers and the black hole leave it */
	keys(&e, "\"ayyj\"_dd");
	CHECK(has("one ") && is(&e, "two\n"));
	/* text another program copied: a put reads it */
	strcpy(clip, "x\ny\n"); clipn = 4;
	keys(&e, "p");
	CHECK(is(&e, "two\nx\ny\n"));
	strcpy(clip, "ab"); clipn = 2;
	keys(&e, "gg0P");
	CHECK(is(&e, "abtwo\nx\ny\n"));
	/* "+ and "* are the same clipboard; "" is the unnamed register
	   without it (adjust_clipboard_name(): only NUL is implicit) */
	keys(&e, "\"*yiw");
	CHECK(has("abtwo"));
	strcpy(clip, "q"); clipn = 1;
	keys(&e, "\"+P");
	CHECK(is(&e, "qabtwo\nx\ny\n"));
	core_free(&e);

	/* a block keeps its type through the clipboard, as Neovim's provider
	   cache keeps it */
	open_with(&e, path, "ab\ncd\n");
	keys(&e, "\x16jy");
	CHECK(has("a\nc\n"));
	keys(&e, "$p");
	CHECK(is(&e, "aba\ncdc\n"));
	core_free(&e);

	/* Visual p puts the clipboard, and the replaced text goes there */
	open_with(&e, path, "ab cd\n");
	strcpy(clip, "XY"); clipn = 2;
	keys(&e, "wviwp");
	CHECK(is(&e, "ab XY\n") && has("cd"));
	core_free(&e);

	/* a clipboard that cannot be read: the unnamed register's last text */
	open_with(&e, path, "ab\n");
	keys(&e, "yl");
	sets = 0;
	clipn = -1;
	keys(&e, "p");
	CHECK(is(&e, "aab\n") && sets == 0);
	core_free(&e);

	unlink(path);
	puts("test_keys_clip: ok");
	return 0;
}
