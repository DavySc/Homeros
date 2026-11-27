/* Review of PR 166: probes of the frame letters, fed as main.c feeds keys.
   Build: reviews/pr-166-repro/build.sh.  Each P() prints what it saw; the
   exit status counts the ones that are not as the PR says. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "core.h"
#include "keys.h"
#include "tools.h"

static int bad;
#define P(name, ok) do { int o_ = (ok); printf("%-4s %s\n", o_ ? "ok" : "BAD", name); bad += !o_; } while (0)

static void feed(Editor *e, const char *s)
{
	for (; *s; s++) keys_input(e, *s == 27 ? KEY_ESC : (unsigned char)*s, s[1] != 0);
}
static int ntab(Editor *e) { int n = 0; for (Tab *t = e->tabs; t; t = t->next) n++; return n; }
static Tab *of(Editor *e, char k) { for (Tab *t = e->tabs; t; t = t->next) if (t->key == k) return t; return NULL; }
static int uniq(Editor *e)
{
	for (Tab *t = e->tabs; t; t = t->next) {
		if (t->key && (t->key < 'a' || t->key > 'z' || strchr("fmno", t->key))) return 0;
		for (Tab *u = t->next; u; u = u->next) if (t->key && t->key == u->key) return 0;
	}
	return 1;
}

int main(void)
{
	setvbuf(stdout, NULL, _IONBF, 0);   /* the sanitizer's abort would lose it */
	char path[] = "/tmp/zprobeXXXXXX";
	int fd = mkstemp(path);
	if (fd < 0 || write(fd, "abcb\nline2\n", 11) != 11 || close(fd)) return 99;
	static Editor e;
	if (core_open(&e, path)) return 99;

	/* 1. every way a tab page is made gives a letter */
	P("first frame is a", e.tab->key == 'a');
	feed(&e, " 52");
	P("SPC 5 2 gives b", e.tab->key == 'b');
	feed(&e, "\x17v\x17T");
	P("CTRL-W T gives c", e.tab->key == 'c' && ntab(&e) == 3);
	feed(&e, ":tabnew\r");
	P(":tabnew gives d (or is no command)", e.tab->key == 'd' || ntab(&e) == 3);
	int n0 = ntab(&e);
	feed(&e, " 5f");
	tools_key(&e, KEY_ESC); tools_key(&e, KEY_ESC);
	P("SPC 5 f still makes a frame, with a letter", ntab(&e) == n0 + 1 && e.tab->key && uniq(&e));

	/* 2. jump to the current frame: stays, no map left half done */
	Tab *cur = e.tab;
	char ck[8];
	snprintf(ck, sizeof ck, " 5%c", cur->key);
	feed(&e, ck);
	P("jump to the current frame stays", e.tab == cur && !e.ks.pending);
	feed(&e, " 5a");
	P("SPC 5 a enters the first", e.tab == e.tabs && !e.ks.pending);

	/* 3. a letter no frame has: as an unmapped SPC 5 x was before */
	feed(&e, "gg0 5q");
	P("SPC 5 q (no frame q) leaves nothing pending", !e.ks.pending && e.tab == e.tabs);
	feed(&e, "\x1b");
	char b0[64] = { 0 };
	feed(&e, "gg0 5x");
	text_read(e.buf->text, 0, b0, 20);
	printf("     after SPC 5 x: [%.5s]\n", b0);
	P("SPC 5 x (no frame x) leaves the text", !strncmp(b0, "abcb\n", 5));
	feed(&e, "\x1bu");

	/* 4. rename, then jump by the new and the old letter */
	feed(&e, " 5b 5nq");
	P("rename b to q", e.tab->key == 'q' && uniq(&e));
	Tab *q = e.tab;
	feed(&e, " 5a 5q");
	P("jump by the new letter", e.tab == q);
	feed(&e, " 5a 5nb");
	P("the first frame may take the freed b", e.tabs->key == 'b' && uniq(&e));
	feed(&e, " 5na");

	/* 4b. SPC 5 m {letter}: into that frame; its own letter and a number beep */
	feed(&e, " 5a\x17v");
	int w0 = 0; for (Win *w = e.wins; w; w = w->next) w0++;
	feed(&e, " 5ma 5m2");
	int w1 = 0; for (Win *w = e.wins; w; w = w->next) w1++;
	P("SPC 5 m {own letter} and SPC 5 m 2 move nothing", w1 == w0 && e.tab == e.tabs);
	feed(&e, " 5mq");
	P("SPC 5 m q moves the window into frame q", e.tab == q);
	feed(&e, "\x17" "c 5a");

	/* 5. rename to its own letter: kept, no beep needed */
	feed(&e, " 5na");
	P("rename to its own letter", e.tabs->key == 'a');

	/* 6. capitals, digits, Esc, keys above ASCII after SPC 5 n */
	feed(&e, " 5nA");
	P("SPC 5 n A refused", e.tab->key == 'a' && !e.ks.pending);
	feed(&e, " 5n\x1b");
	P("SPC 5 n Esc refused, nothing pending", e.tab->key == 'a' && !e.ks.pending);
	keys_input(&e, ' ', 1); keys_input(&e, '5', 1); keys_input(&e, 'n', 1); keys_input(&e, KEY_UP, 0);
	P("SPC 5 n <Up> refused", e.tab->key == 'a' && !e.ks.pending);
	keys_input(&e, ' ', 1); keys_input(&e, '5', 1); keys_input(&e, 'n', 1); keys_input(&e, 0xe9, 0);
	P("SPC 5 n e-acute refused", e.tab->key == 'a' && !e.ks.pending);
	keys_input(&e, ' ', 1); keys_input(&e, '5', 1); keys_input(&e, 'n', 1); keys_input(&e, 0x100 + 'c', 0);
	P("SPC 5 n U+0163 refused (not truncated to c)", e.tab->key == 'a' && !e.ks.pending);

	/* 7. KEY_FOCUS between SPC 5 n and the letter ends it */
	feed(&e, " 5n");
	keys_input(&e, KEY_FOCUS, 0);
	feed(&e, "z");
	P("focus change ends SPC 5 n", e.tab->key == 'a');
	feed(&e, "\x1b");

	/* 8. more frames than letters, and the key 0 (Ctrl-@) */
	feed(&e, " 51");
	for (int i = 0; i < 24; i++) core_tabnew(&e, NULL);
	P("25 frames: letters unique, 22 lettered", uniq(&e) && ntab(&e) == 25);
	int nolet = 0;
	for (Tab *t = e.tabs; t; t = t->next) nolet += !t->key;
	P("3 frames without a letter", nolet == 3);
	/* SPC 5 m Ctrl-@: SPC 5 Ctrl-@ itself stops at package 5's over-read
	   (test_zprobe_nul.c), so the key 0 is tried after SPC 5 m */
	feed(&e, " 5a");
	cur = e.tab;
	keys_input(&e, ' ', 1); keys_input(&e, '5', 1); keys_input(&e, 'm', 1); keys_input(&e, 0, 0);
	P("SPC 5 m Ctrl-@ (key 0) moves nothing into a frame without a letter", e.tab == cur && !e.ks.pending);
	/* a frame without a letter gets the letter of one closed, by SPC 5 n only */
	feed(&e, " 5z 50");
	Tab *t0 = e.tabs;
	while (t0 && t0->key) t0 = t0->next;
	P("closing z does not give z to a frame without a letter (by design: SPC 5 n does)", t0 && !of(&e, 'z'));
	core_tabnew(&e, NULL);
	P("the next new frame takes z", e.tab->key == 'z');

	/* 9. in a frame with no letter, SPC 5 n takes none while all are held */
	core_tab_enter(&e, t0);
	feed(&e, " 5nb");
	P("no letter free: refused", !e.tab->key);

	/* 10. Visual and Insert mode: SPC 5 x is not a map there */
	feed(&e, " 5a");
	feed(&e, "v 5c");
	printf("     Visual, SPC 5 c: tab key %c visual %d\n", e.tab->key, e.ks.visual);
	P("Visual mode: SPC 5 c does not leave the frame", e.tab->key == 'a');
	feed(&e, "\x1b\x1bu");

	/* 11. undo blocks around a jump as around SPC 5 o */
	feed(&e, " 51");
	core_tabnew(&e, NULL);
	feed(&e, " 5a");
	char b1[64] = { 0 }, b2[64] = { 0 };
	feed(&e, "ggx 5bx 5axu");
	text_read(e.buf->text, 0, b1, 20);
	feed(&e, "uuu");
	feed(&e, "ggx 5ox 5oxu");
	text_read(e.buf->text, 0, b2, 20);
	printf("     letter jump [%.8s] other-frame [%.8s]\n", b1, b2);
	P("undo after x, jump, x, jump, x, u: as with SPC 5 o", !strcmp(b1, b2));

	/* 12. counts: 2 SPC 5 a */
	feed(&e, "\x1b" "3 5b");
	printf("     3 SPC 5 b: tab key %c pending %d\n", e.tab->key, e.ks.pending);
	feed(&e, "\x1b");

	core_free(&e);
	unlink(path);
	printf("%d not as the PR says\n", bad);
	return bad;
}
