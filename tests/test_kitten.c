/* The save kitten (src/kitten/): the frames Davy's my-nvim draws
   (tests/test_kitten.txt, recorded from Neovim by tests/test_kitten.py),
   their timing on the real loop, and what starts and stops it. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "check.h"
#include "core.h"
#include "kitten.h"
#include "loop.h"

static Editor e;
static Grid g;
static char dir[] = "/tmp/kittenXXXXXX", path[512];

static long long ms(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000LL + t.tv_nsec / 1000000;
}

/* As main.c draws: the core (an empty buffer: "~" rows under the
   float, as in Neovim), then the kitten. */
static void draw(void)
{
	core_draw(&e, &g);
	kitten_draw(&e, &g);
}

/* Row r as test_kitten.txt writes it: the cells' text, then "|". */
static const char *row(int r)
{
	static char s[4096];
	size_t n = 0;
	for (int c = 0; c < g.cols; c++) {
		const Cell *x = &g.c[r * g.cols + c];
		memcpy(s + n, grid_text(x), x->len);
		n += x->len;
	}
	s[n++] = '|';
	s[n] = 0;
	return s;
}

static void open_file(const char *name, int rows, int cols)
{
	snprintf(path, sizeof path, "%s/%s", dir, name);
	CHECK(!core_open(&e, path));
	CHECK(!grid_resize(&g, rows, cols));
}

/* Waits for the kitten's timer, at most 3 s; returns how long it took. */
static long long tick(void)
{
	long long t0 = ms();
	core_changed = 0;
	while (!core_changed && ms() - t0 < 3000) CHECK(!loop_once(3000));
	CHECK(core_changed);
	return ms() - t0;
}

/* Every recorded case: the six frames, each on screen for kitten.lua's
   time, then the message. */
static void frames(void)
{
	static const int want_ms[] = { 140, 98, 140, 140, 140, 1400 };
	FILE *f = fopen("tests/test_kitten.txt", "r");
	CHECK(f);
	char line[4096], name[1024];
	int rows = 0, cols = 0, fr = -1, k = 0, ncase = 0;
	for (;;) {
		char *got = fgets(line, sizeof line, f);
		if (got) line[strcspn(line, "\n")] = 0;
		if (!got || !strncmp(line, "case ", 5)) {
			if (fr >= 0) {
				/* the last frame was checked: its time, then the message */
				CHECK(fr == 5 && k == 3);
				long long t = tick();
				CHECK(t >= want_ms[5] && t < want_ms[5] + 35);
				char want[1100];
				snprintf(want, sizeof want, "=^.^= saved %s", name);
				CHECK(!strncmp(e.msg, want, sizeof e.msg - 1));
				draw();
				core_free(&e);
				ncase++;
			}
			if (!got) break;
			int at;
			CHECK(sscanf(line, "case %d %d %n", &rows, &cols, &at) == 2);
			snprintf(name, sizeof name, "%s", line + at);
			open_file(name, rows, cols);
			kitten_paw(&e);
			fr = -1;
			continue;
		}
		if (line[0] == '#') continue;
		if (!strcmp(line, "frame")) {
			/* within 35 ms: the blink (98 ms) must not pass for a frame (140) */
			if (fr >= 0) {
				long long t = tick();
				CHECK(t >= want_ms[fr] && t < want_ms[fr] + 35);
			}
			fr++;
			k = 0;
			draw();
			continue;
		}
		const char *have = row(rows - 5 + k++);
		if (strcmp(have, line)) { fprintf(stderr, "%dx%d %s frame %d row %d:\n got  %s\n want %s\n", rows, cols, name, fr, k, have, line); exit(1); }
	}
	fclose(f);
	CHECK(ncase >= 10);
}

static int face_at(int r, int c) { return g.c[r * g.cols + c].face; }

/* kitten.lua's colours by role, and the message's after the end. */
static void colours(void)
{
	open_file("notes.org", 12, 40);
	kitten_paw(&e);
	for (int i = 0; i < 5; i++) { draw(); tick(); }
	draw();
	/* " /\_/\   <3", "( ^.^ )__o", " > ^ <    saved notes.org" from column 13 */
	CHECK(face_at(7, 14) == FACE_KITTEN_CAT && face_at(7, 22) == FACE_KITTEN_HEART);
	CHECK(face_at(8, 13) == FACE_KITTEN_CAT && face_at(8, 15) == FACE_KITTEN_FACE);
	CHECK(face_at(8, 19) == FACE_KITTEN_CAT && face_at(8, 20) == FACE_KITTEN_PAW);
	CHECK(face_at(9, 15) == FACE_KITTEN_CAT && face_at(9, 23) == FACE_KITTEN_TEXT);
	CHECK(face_at(9, 22) == FACE_NONE);
	tick();
	CHECK(!strcmp(e.msg, "=^.^= saved notes.org"));
	draw();
	CHECK(face_at(11, 0) == FACE_KITTEN_CAT && face_at(11, 4) == FACE_KITTEN_CAT);
	CHECK(face_at(11, 5) == FACE_KITTEN_DIM && face_at(11, 11) == FACE_KITTEN_DIM);
	CHECK(face_at(11, 12) == FACE_KITTEN_TEXT && face_at(11, 20) == FACE_KITTEN_TEXT);
	/* another message: no colours */
	/* a non-ASCII name: the message is the core's, cell for cell (no bytes
	   left over), and tinted to its end */
	core_msg(&e, "=^.^= saved 全角.md");
	draw();
	CHECK(!strcmp(row(11), "=^.^= saved 全角.md                     |"));
	CHECK(face_at(11, 12) == FACE_KITTEN_TEXT && face_at(11, 18) == FACE_KITTEN_TEXT && face_at(11, 19) == FACE_NONE);
	core_msg(&e, "\"notes.org\" 0L, 0B written");
	draw();
	CHECK(face_at(11, 0) == FACE_NONE);
	core_free(&e);
}

/* Blank where the kitten would be: nothing drawn. */
static int gone(void)
{
	draw();
	return !strcmp(row(g.rows - 4), "~                                       |");
}

/* A timer that waits a while sees nothing from the kitten. */
static int quiet(int t)
{
	long long t0 = ms();
	core_changed = 0;
	/* waits it out: a save's own file-watch event wakes the loop */
	for (long long left; (left = t - (ms() - t0)) > 0; ) CHECK(!loop_once((int)left));
	return !core_changed;
}

static void rules(void)
{
	open_file("notes.org", 12, 40);
	/* a save of the current buffer plays it, after the written message */
	CHECK(!core_write(&e, e.buf, 0));
	CHECK(strstr(e.msg, " written"));
	CHECK(!gone());
	/* any key: gone, and its timer with it */
	kitten_key();
	CHECK(gone());
	CHECK(quiet(300));
	/* a save while it plays for the same buffer starts nothing again */
	CHECK(!core_write(&e, e.buf, 0));
	draw();
	tick();
	draw();
	CHECK(strstr(row(8), "-.-"));
	CHECK(!core_write(&e, e.buf, 0));
	draw();
	CHECK(strstr(row(8), "-.-"));
	kitten_key();
	/* another buffer's save (:wa): nothing */
	Buf other = *e.buf;
	kitten_written(&e, &other);
	CHECK(gone());
	/* a save from a macro (@q running :w): nothing */
	e.ks.executing = 1;
	CHECK(!core_write(&e, e.buf, 0));
	e.ks.executing = 0;
	CHECK(gone());
	/* <leader>uk: off, then on */
	kitten_toggle(&e);
	CHECK(!strcmp(e.msg, "Kitten on save: off"));
	CHECK(!core_write(&e, e.buf, 0));
	CHECK(gone());
	kitten_toggle(&e);
	CHECK(!strcmp(e.msg, "Kitten on save: on"));
	CHECK(!core_write(&e, e.buf, 0));
	CHECK(!gone());
	/* hidden (no draws): one wake for the frame due, then none */
	tick();
	CHECK(quiet(400));
	/* shown again: it goes on from there */
	draw();
	CHECK(strstr(row(8), "-.-"));
	CHECK(tick() < 200);
	/* redraws every 20 ms (other things on screen) do not hold it back:
	   the next frames still come at 140 ms */
	draw();
	long long t0 = ms();
	while (!strstr(row(8), "^.^")) {
		CHECK(ms() - t0 < 400);
		core_changed = 0;
		CHECK(!loop_once(20));
		draw();
	}
	kitten_key();
	core_free(&e);
}

/* Every frame on every screen from 1 x 1 to 6 x 18 (the sanitizers say if
   a cell outside the grid is touched), and a taller command line. */
static void small(void)
{
	open_file("全角ファイル名.md", 1, 1);
	kitten_paw(&e);
	for (int i = 0; i < 6; i++) {
		for (int rows = 1; rows <= 6; rows++)
			for (int cols = 1; cols <= 18; cols++) {
				CHECK(!grid_resize(&g, rows, cols));
				draw();
			}
		CHECK(!grid_resize(&g, 24, 80));
		draw();      /* arms this frame's timer */
		if (i < 5) tick();
	}
	kitten_key();
	core_free(&e);
	/* cmdheight 3 (resize -2 in one window): Neovim puts the float at row 3
	   of 10 x 40, above the status line, and the message on row 7 */
	open_file("notes.org", 10, 40);
	core_draw(&e, &g);
	core_resize(&e, 0, -2, 0);
	kitten_paw(&e);
	draw();
	CHECK(strstr(row(4), "( o.o )") && strstr(row(5), " > ^ < "));
	for (int i = 0; i < 6; i++) { tick(); draw(); }
	CHECK(face_at(7, 0) == FACE_KITTEN_CAT && face_at(9, 0) == FACE_NONE);
	core_free(&e);
}

int main(void)
{
	CHECK(mkdtemp(dir));
	frames();
	colours();
	rules();
	small();
	char cmd[600];
	snprintf(cmd, sizeof cmd, "rm -rf %s", dir);
	CHECK(!system(cmd));
	grid_free(&g);
	puts("test_kitten: the frames of my-nvim's kitten, its timing and rules");
	return 0;
}
