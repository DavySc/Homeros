/* The save kitten on Windows (src/kitten/check.sh, under Wine): the same
   timing as test_kitten.c on the Windows loop (src/loop/win.c), a hidden
   window woken once at most, redraws that do not hold it back, and a file
   name after a backslash.  The core is stubbed: only core_msg and
   core_changed are used.  Elsewhere it says so. */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "check.h"
#include "core.h"
#include "kitten.h"
#include "loop.h"

int core_changed;
void core_msg(Editor *e, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(e->msg, sizeof e->msg, fmt, ap);
	va_end(ap);
}

static Editor e;
static Buf b;
static Win w = { .y = 0, .h = 10 };   /* its status line on row 10, the command line on 11 */
static Grid g;

static long long ms(void) { return (long long)GetTickCount64(); }

static void draw(void)
{
	grid_clear(&g);
	kitten_draw(&e, &g);
}

/* the left eye: two cells after the "(" on the kitten's middle row */
static int eyes(const char *s)
{
	for (int c = 0; c + 2 < g.cols; c++)
		if (g.c[8 * g.cols + c].ch[0] == '(') return g.c[8 * g.cols + c + 2].ch[0] == s[0];
	return 0;
}

static long long tick(void)
{
	long long t0 = ms();
	core_changed = 0;
	while (!core_changed && ms() - t0 < 3000) CHECK(!loop_once(3000));
	CHECK(core_changed);
	return ms() - t0;
}

static int quiet(int t)
{
	long long t0 = ms();
	core_changed = 0;
	CHECK(!loop_once(t));
	return !core_changed && ms() - t0 >= t - 20;
}

int main(void)
{
	static const int want[] = { 140, 98, 140, 140, 140, 1400 };
	static const char *eye = "o-o^^^";
	char path[] = "C:\\Users\\davy\\notes.org";
	b.path = path;
	e.buf = &b;
	e.wins = &w;
	CHECK(!grid_resize(&g, 12, 40));
	kitten_paw(&e);
	for (int i = 0; i < 6; i++) {
		draw();
		CHECK(eyes((char[]){ eye[i], 0 }));
		/* GetTickCount64 steps by about 16 ms */
		long long t = tick();
		CHECK(t >= want[i] - 16 && t < want[i] + 100);
	}
	CHECK(!strcmp(e.msg, "=^.^= saved notes.org"));
	/* hidden after the first frame: the frame due wakes it, then nothing */
	kitten_paw(&e);
	draw();
	tick();
	CHECK(quiet(400));
	draw();
	CHECK(eyes("-"));
	CHECK(tick() < 200);
	/* redraws every 20 ms do not hold the frames back */
	draw();
	long long t0 = ms();
	while (!eyes("^")) {
		CHECK(ms() - t0 < 400);
		core_changed = 0;
		CHECK(!loop_once(20));
		draw();
	}
	/* a key: gone, and no more wakes */
	kitten_key();
	draw();
	CHECK(!eyes("^") && !eyes("o"));
	CHECK(quiet(300));
	grid_free(&g);
	puts("test_kitten_win: the kitten's frames, timing and wakes on the Windows loop");
	return 0;
}
#else
#include <stdio.h>
int main(void) { puts("test_kitten_win: Windows only (src/kitten/check.sh runs it under Wine)"); return 0; }
#endif
