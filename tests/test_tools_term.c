/* The terminal end to end: the real loop, sh on a pseudo-terminal,
   libghostty-vt, keys through tools_key and the screen read back from the
   drawn grid.  Ctrl+. opens, hides and shows it again with the same shell;
   colours (palette, Catppuccin's ANSI, 24-bit, inverse) and styles become
   faces; the title, the cursor's shape and COLORTERM reach the screen;
   cursor keys follow DECCKM, Delete is xterm's; the wheel scrolls back and
   a key returns; a wide character takes two cells; the screen follows the
   editor's size; more colours than slots fall back without leaving a cell
   in a reused slot's old colours; a hidden shell redraws nothing; exit
   closes it. */
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "check.h"
#include "core.h"
#include "keys.h"
#include "loop.h"
#include "tools.h"
static Editor e;
static Grid g, shown;

static long long ms(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000LL + t.tv_nsec / 1000000;
}

/* until 300 ms pass with no output from the shell */
static void settle(void)
{
	long long t0 = ms();
	while (ms() - t0 < 300) {
		core_changed = 0;
		CHECK(!loop_once(300));
		if (core_changed) t0 = ms();
	}
}

/* A frame, and the rule that lets a slot be reused: a slot the last frame
   (what the backend shows) holds keeps its colours, else a cell the
   backend skips as unchanged would show the old ones. */
static void draw(void)
{
	static Face was[FACES];
	static unsigned char held[FACES];
	memcpy(was, faces, sizeof was);
	core_draw(&e, &g);
	tools_draw(&e, &g);
	for (int k = FACE_COUNT; k < FACES; k++) CHECK(!held[k] || !memcmp(&was[k], &faces[k], sizeof was[k]));
	memset(held, 0, sizeof held);
	for (int i = 0; i < g.rows * g.cols; i++) held[g.c[i].face] = 1;
}

static void type(const char *s)
{
	for (; *s; s++) CHECK(tools_key(&e, *s == '\n' ? KEY_ENTER : (unsigned char)*s));
	settle();
	draw();
}

/* the cell where s starts on the screen, the lowest row first (output
   comes below the command line that names it), or NULL */
static const Cell *find(const char *s)
{
	size_t n = strlen(s);
	for (int r = g.rows; r-- > 0;)
		for (int c = 0; c + (int)n <= g.cols; c++) {
			size_t k = 0;
			while (k < n && g.c[r * g.cols + c + (int)k].len == 1 && g.c[r * g.cols + c + (int)k].ch[0] == (unsigned char)s[k]) k++;
			if (k == n) return &g.c[r * g.cols + c];
		}
	return NULL;
}

static int fg(const Cell *x, int r, int gr, int b)
{
	const Face *f, *bg;
	f = grid_face(x, &bg);
	return (f->style & FACE_FG) && f->fg[0] == r && f->fg[1] == gr && f->fg[2] == b;
}

/* x's face has the style bits s and none of the bits not */
static int style(const Cell *x, int s, int not)
{
	const Face *bg, *f = grid_face(x, &bg);
	return (f->style & (s | not)) == s;
}

/* the number n of the topmost "Ln" line on the screen; 0 none */
static int top(void)
{
	int i = 0, n = 0, end = g.rows * g.cols - 1;
	while (i < end && !(g.c[i].ch[0] == 'L' && g.c[i + 1].ch[0] >= '0' && g.c[i + 1].ch[0] <= '9')) i++;
	while (++i <= end && g.c[i].ch[0] >= '0' && g.c[i].ch[0] <= '9') n = n * 10 + g.c[i].ch[0] - '0';
	return n;
}

int main(void)
{
	setenv("SHELL", "/bin/sh", 1);
	setenv("PS1", "$ ", 1);
	setenv("ENV", "/dev/null", 1);
	setenv("COLORTERM", "no", 1);   /* the pty's own must win */
	CHECK(!core_open(&e, NULL));
	CHECK(!grid_resize(&g, 30, 100));
	draw();
	CHECK(tools_key(&e, KEY_CTRL_DOT));
	settle();
	draw();
	CHECK(find(" Terminal "));
	/* the float: snacks' 90%, a rounded border */
	CHECK(g.c[(29 - 27) / 2 * g.cols + 5].len == 3 && !memcmp(g.c[(29 - 27) / 2 * g.cols + 5].ch, "\xe2\x95\xad", 3));

	/* colours: ANSI red is Catppuccin's, 24-bit is exact, inverse swaps
	   onto the default colours */
	type("printf '\\033[31mred\\033[0m \\033[38;2;1;2;3mtrue\\033[0m \\033[7minv\\033[0m\\n'\n");
	const Cell *x = find("red");
	CHECK(x && fg(x, 243, 139, 168) && fg(x + 2, 243, 139, 168));
	CHECK((x = find("true")) && fg(x, 1, 2, 3));
	CHECK((x = find("inv")) && fg(x, 30, 30, 46));
	const Face *b;
	grid_face(x, &b);
	CHECK((b->style & FACE_BG) && b->bg[0] == 205 && b->bg[1] == 214 && b->bg[2] == 244);
	/* a wide character takes two cells */
	type("printf '\\346\\274\\242Z\\n'\n");
	CHECK((x = find("Z")) && x[-2].len == 3 && x[-1].len == 0);
	/* styles: bold, strikethrough, undercurl apart from underline, and
	   invisible as the background (SGR 8) */
	type("printf '\\033[1mBO\\033[0m \\033[9mST\\033[0m \\033[4:3mUC\\033[0m \\033[4mUL\\033[0m \\033[8mIV\\033[0m\\n'\n");
	CHECK((x = find("BO")) && style(x, FACE_BOLD, 0));
	CHECK((x = find("ST")) && style(x, FACE_STRIKE, 0));
	CHECK((x = find("UC")) && style(x, FACE_UNDERCURL, FACE_UNDERLINE));
	CHECK((x = find("UL")) && style(x, FACE_UNDERLINE, FACE_UNDERCURL));
	CHECK((x = find("IV")) && fg(x, 30, 30, 46));
	/* the OSC title on the border (octal, so the command line does not
	   show it), the cursor's shape (DECSCUSR), true colour advertised */
	type("printf '\\033]2;t\\164l\\007\\033[5 q'; echo $COLORTERM\n");
	CHECK(find(" ttl ") && find("truecolor") && g.cshape == CURSOR_BAR);
	type("printf '\\033[4 q'\n");
	CHECK(g.cshape == CURSOR_UNDER);
	/* Delete is xterm's */
	type("cat -v\n");
	CHECK(tools_key(&e, KEY_DEL));
	type("\n");
	CHECK(find("^[[3~"));
	CHECK(tools_key(&e, 4));
	/* the wheel scrolls back 3 lines a notch; a key goes back to the end */
	type("for i in $(seq 100); do echo L$i; done\n");
	int t0 = top();
	CHECK(t0 > 1 && tools_key(&e, KEY_WHEEL_UP));
	draw();
	CHECK(top() == t0 - 3);
	type(" ");
	CHECK(top() == t0);
	CHECK(tools_key(&e, 21));   /* CTRL-U drops the space */
	settle();

	/* cursor keys: CSI normally, SS3 in application mode (DECCKM) */
	type("printf '\\033[?1h'; cat -v\n");
	CHECK(tools_key(&e, KEY_UP));
	type("\n");
	CHECK(find("^[OA"));
	CHECK(tools_key(&e, 4));   /* CTRL-D ends cat */
	type("printf '\\033[?1l'; cat -v\n");
	CHECK(tools_key(&e, KEY_LEFT));
	type("\n");
	CHECK(find("^[[D"));
	CHECK(tools_key(&e, 4));
	settle();

	/* the editor's size is the terminal's, 90% of it less the border */
	CHECK(!grid_resize(&g, 40, 120));
	draw();
	type("stty size\n");
	CHECK(find("34 106"));

	/* more colours than free slots: none is left in a reused slot's old
	   colours, every 24-bit cell in a slot holds its own colour */
	for (int round = 0; round < 3; round++) {
		char cmd[160];
		snprintf(cmd, sizeof cmd, "printf '\\033[H\\033[2J'; for i in $(seq %d %d); do printf '\\033[38;2;%%d;%%d;9m#' $((i/256)) $((i%%256)); done; echo\n",
		         round * 500, round * 500 + 999);
		type(cmd);
		draw();   /* a slot the shown frame holds is free one frame later */
		int seen = 0;
		for (int i = 0; i < g.rows * g.cols; i++)
			if (g.c[i].len == 1 && g.c[i].ch[0] == '#' && g.c[i].face >= FACE_COUNT) {
				CHECK(faces[g.c[i].face].fg[2] == 9);
				seen++;
			}
		CHECK(seen == FACES - FACE_COUNT);
	}

	/* Ctrl+. hides it: keys go to the editor again; it comes back with the
	   same shell and screen */
	type("echo kept\n");
	CHECK(tools_key(&e, KEY_CTRL_DOT));
	draw();
	CHECK(!find("kept") && !tools_key(&e, 'x'));
	/* a hidden shell's output does not redraw the editor */
	CHECK(tools_key(&e, KEY_CTRL_DOT));
	type("sleep 0.6; echo late\n");
	CHECK(tools_key(&e, KEY_CTRL_DOT));
	core_changed = 0;
	for (long long t = ms(); ms() - t < 1000;) CHECK(!loop_once(100));
	CHECK(!core_changed);
	CHECK(tools_key(&e, KEY_CTRL_DOT));
	draw();
	CHECK(find("kept"));
	/* the shell's exit closes it */
	type("exit\n");
	settle();
	CHECK(!tools_key(&e, 'x'));
	draw();
	CHECK(!find("kept"));
	/* and Ctrl+. starts a new one */
	CHECK(tools_key(&e, KEY_CTRL_DOT));
	settle();
	type("echo again\n");
	CHECK(find("again"));
	type("exit\n");
	settle();
	grid_free(&g);
	grid_free(&shown);
	core_free(&e);
	return 0;
}
