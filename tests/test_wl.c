/* Package 8: the Wayland backend end to end.  Under headless sway (wlroots
   with the pixman renderer; like Hyprland it has a seat, text-input-v3,
   cursor-shape, fractional scale and data-control): configure size, scale,
   keys, key repeat, clipboard both ways, input method, pointer shape, frame
   callbacks while shown and hidden, an image's cells.  Under headless weston
   16 (its kiosk shell configures the output's size; it has no seat):
   configure size and frame callbacks.  Keys come from wtype (a virtual
   keyboard), the clipboard's other side is wl-copy and wl-paste, the input
   method is this test (input-method-v2). */
#define _GNU_SOURCE
#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>
#include "check.h"
#include "backend.h"
#include "keys.h"
#include "font.h"
#include "image.h"
enum { BTN_LEFT = 0x110 };   /* linux/input-event-codes.h clashes with keys.h */
/* the backend's cell, from its font */
static int cellw(void) { int w, h, b; font_cell(&w, &h, &b); return w; }
static int cellh(void) { int w, h, b; font_cell(&w, &h, &b); return h; }
/* the glue of the two protocols only this test uses */
#include "input-method-unstable-v2.c"
#include "wlr-virtual-pointer-unstable-v1.c"
#include "input-method-unstable-v2-client-protocol.h"
#include "wlr-virtual-pointer-unstable-v1-client-protocol.h"

static char dir[64];
static pid_t cpid;
static int keys[8192], wins[8192], nkeys;   /* wins: the window each key came from */
static Grid g, shown;

static double now(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

/* ---- the input method ---- */

static struct wl_display *imd;
static struct wl_seat *imseat;
static struct zwp_input_method_manager_v2 *imm;
static struct zwp_input_method_v2 *im;
static struct wl_registry *imreg;
static struct zwlr_virtual_pointer_manager_v1 *vpm;
static int im_active, im_pending, im_dones;

static void im_activate(void *d, struct zwp_input_method_v2 *m) { (void)d; (void)m; im_pending = 1; }
static void im_deactivate(void *d, struct zwp_input_method_v2 *m) { (void)d; (void)m; im_pending = 0; }
static void im_surrounding(void *d, struct zwp_input_method_v2 *m, const char *t, uint32_t c, uint32_t a) { (void)d; (void)m; (void)t; (void)c; (void)a; }
static void im_cause(void *d, struct zwp_input_method_v2 *m, uint32_t c) { (void)d; (void)m; (void)c; }
static void im_content(void *d, struct zwp_input_method_v2 *m, uint32_t h, uint32_t p) { (void)d; (void)m; (void)h; (void)p; }
static void im_done(void *d, struct zwp_input_method_v2 *m) { (void)d; (void)m; im_dones++; im_active = im_pending; }
static void im_unavailable(void *d, struct zwp_input_method_v2 *m) { (void)d; (void)m; CHECK(!"input method unavailable"); }
static const struct zwp_input_method_v2_listener im_l = { im_activate, im_deactivate, im_surrounding, im_cause, im_content, im_done, im_unavailable };

static void im_reg_add(void *d, struct wl_registry *r, uint32_t id, const char *i, uint32_t v)
{
	(void)d; (void)v;
	if (!strcmp(i, wl_seat_interface.name)) imseat = wl_registry_bind(r, id, &wl_seat_interface, 1);
	if (!strcmp(i, zwp_input_method_manager_v2_interface.name)) imm = wl_registry_bind(r, id, &zwp_input_method_manager_v2_interface, 1);
	if (!strcmp(i, zwlr_virtual_pointer_manager_v1_interface.name)) vpm = wl_registry_bind(r, id, &zwlr_virtual_pointer_manager_v1_interface, 1);
}
static void im_reg_rm(void *d, struct wl_registry *r, uint32_t id) { (void)d; (void)r; (void)id; }
static const struct wl_registry_listener im_reg_l = { im_reg_add, im_reg_rm };

/* ---- driving ---- */

/* Runs the backend (and the input method) for ms, collecting keys. */
static void pump(int ms)
{
	int fds[2];
	backend_wl.fds(fds);
	struct pollfd p[3] = { { fds[0], POLLIN, 0 }, { fds[1], POLLIN, 0 }, { imd ? wl_display_get_fd(imd) : -1, POLLIN, 0 } };
	for (double end = now() + ms; now() < end;) {
		if (imd) wl_display_flush(imd);
		if (poll(p, 3, (int)(end - now()) + 1) <= 0) continue;
		if (p[0].revents || p[1].revents) nkeys += backend_wl.input(keys + nkeys, wins + nkeys, (int)(sizeof keys / sizeof *keys) - nkeys);
		if (p[2].revents) CHECK(wl_display_dispatch(imd) >= 0);
	}
}

/* Runs a shell command while the backend keeps answering the compositor;
   it must exit 0 within 5 s. */
static void run(const char *cmd)
{
	pid_t pid = fork();
	CHECK(pid >= 0);
	if (!pid) { execl("/bin/sh", "sh", "-c", cmd, (char *)NULL); _exit(127); }
	int st = 0;
	for (double end = now() + 5000; waitpid(pid, &st, WNOHANG) == 0;) {
		if (now() > end) { kill(pid, SIGKILL); waitpid(pid, &st, 0); fprintf(stderr, "timeout: %s\n", cmd); CHECK(0); }
		pump(5);
	}
	if (!WIFEXITED(st) || WEXITSTATUS(st)) fprintf(stderr, "failed: %s\n", cmd);
	CHECK(WIFEXITED(st) && !WEXITSTATUS(st));
}

/* fmt has arg in it up to five times */
static void cmd(const char *fmt, const char *arg)
{
	char b[512];
	snprintf(b, sizeof b, fmt, arg, arg, arg, arg, arg);
	run(b);
}

static int count(int k, int from)
{
	int n = 0;
	for (int i = from; i < nkeys; i++) n += keys[i] == k;
	return n;
}

/* Draws a frame of the backend's size and waits for its frame callback. */
static void show(void);
static void draw(void)
{
	int r, c;
	backend_wl.size(0, &r, &c);
	if (g.rows != r || g.cols != c) CHECK(!grid_resize(&g, r, c));
	grid_puts(&g, 0, 0, g.cols, "homeros", 7, ATTR_NONE, FACE_NONE);
	g.crow = 0;
	g.ccol = 7;
	show();
}

/* Presents g as it is, once a frame may be drawn, and waits for the callback. */
static void show(void)
{
	for (double end = now() + 2000; !backend_wl.ready(0) && now() < end;) pump(5);
	CHECK(backend_wl.ready(0));
	backend_wl.present(0, &g, &shown);
	CHECK(!backend_wl.ready(0));     /* Present.tla: nothing more until the callback */
	for (double end = now() + 2000; !backend_wl.ready(0) && now() < end;) pump(5);
	CHECK(backend_wl.ready(0));
}

static void start(const char *fmt)
{
	snprintf(dir, sizeof dir, "/tmp/wltest-XXXXXX");
	CHECK(mkdtemp(dir));
	setenv("XDG_RUNTIME_DIR", dir, 1);
	unsetenv("WAYLAND_DISPLAY");
	char b[1024];
	snprintf(b, sizeof b, fmt, dir, dir);
	cpid = fork();
	CHECK(cpid >= 0);
	if (!cpid) { setpgid(0, 0); execl("/bin/sh", "sh", "-c", b, (char *)NULL); _exit(127); }
	setpgid(cpid, cpid);
	for (double end = now() + 10000; now() < end; usleep(10000)) {
		DIR *d = opendir(dir);
		struct dirent *e;
		while (d && (e = readdir(d)))
			if (!strncmp(e->d_name, "wayland-", 8) && !strstr(e->d_name, ".lock")) {
				setenv("WAYLAND_DISPLAY", e->d_name, 1);
				closedir(d);
				return;
			}
		if (d) closedir(d);
	}
	fprintf(stderr, "no compositor socket: %s\n", b);
	CHECK(0);
}

/* The compositor's whole process group (nix's sway runs under
   dbus-run-session, with swaybg beside it); also at exit after a failed CHECK. */
static void stop(void)
{
	if (!cpid) return;
	kill(-cpid, SIGTERM);
	waitpid(cpid, NULL, 0);
	cpid = 0;
	char b[128];
	snprintf(b, sizeof b, "rm -rf %s", dir);
	CHECK(!system(b));
}

static void size_is(int lw, int lh, int scale120)
{
	int r, c, bw = (lw * scale120 + 60) / 120, bh = (lh * scale120 + 60) / 120;
	backend_wl.size(0, &r, &c);
	if (r != bh / cellh() || c != bw / cellw())
		fprintf(stderr, "size %dx%d cells, want %dx%d (%dx%d px at %d/120)\n", r, c, bh / cellh(), bw / cellw(), lw, lh, scale120);
	CHECK(r == bh / cellh() && c == bw / cellw());
}

#define SWAYMSG "SWAYSOCK=$(echo $XDG_RUNTIME_DIR/sway-ipc.*) swaymsg -q "
/* Do a.ppm and b.ppm (40x40 screenshots) differ in their first rows? */
static int above(int rows)
{
	char a[80], b[80];
	unsigned char x[40 * 40 * 3], y[40 * 40 * 3];
	snprintf(a, sizeof a, "%s/a.ppm", dir);
	snprintf(b, sizeof b, "%s/b.ppm", dir);
	FILE *f = fopen(a, "rb"), *h = fopen(b, "rb");
	CHECK(f && h);
	CHECK(fscanf(f, "P6 40 40 255%*c") == 0 && fread(x, 1, sizeof x, f) == sizeof x);
	CHECK(fscanf(h, "P6 40 40 255%*c") == 0 && fread(y, 1, sizeof y, h) == sizeof y);
	fclose(f);
	fclose(h);
	return memcmp(x, y, (size_t)rows * 40 * 3) != 0;
}

/* How many pixels of the screen at (x, y), w by h, are not the window's
   background; with warm 1, only those far redder than blue (an emoji's
   skin, which no face's colour is); with 2, those not its corner's colour
   (a cell's ink on any background) */
static int ink(int x, int y, int w, int h, int warm)
{
	char b[512];
	snprintf(b, sizeof b, "grim -g '%d,%d %dx%d' -t ppm %s/i.ppm", x, y, w, h, dir);
	run(b);
	snprintf(b, sizeof b, "%s/i.ppm", dir);
	FILE *f = fopen(b, "rb");
	int pw, ph, n = 0;
	CHECK(f && fscanf(f, "P6 %d %d 255%*c", &pw, &ph) == 2);
	for (int i = 0, c, k = 0; i < pw * ph; i++) {
		unsigned char p[3];
		CHECK(fread(p, 1, 3, f) == 3);
		c = p[0] << 16 | p[1] << 8 | p[2];
		if (!i) k = c;
		n += warm == 1 ? p[0] > p[2] + 64 : c != (warm ? k : 0x1e1e2e);
	}
	fclose(f);
	return n;
}

static const char SWAY[] =
	"cat > %s/sway.cfg <<'E'\n"
	"output HEADLESS-1 resolution 1280x800 scale 1\n"
	"default_border none\n"
	"focus_on_window_activation focus\n"   /* Hyprland's focus_on_activate */
	"seat * xcursor_theme Adwaita 24\n"
	"input type:keyboard {\nrepeat_delay 300\nrepeat_rate 25\n}\n"
	"E\n"
	"export XCURSOR_PATH=$(dirname $(command -v sway))/../share/icons\n"
	"WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 exec sway -c %s/sway.cfg >/dev/null 2>&1";

static void test_sway(void)
{
	start(SWAY);
	/* a keyboard for the whole test, so the seat never loses it */
	pid_t kb = fork();
	CHECK(kb >= 0);
	if (!kb) { setpgid(0, cpid); execl("/bin/sh", "sh", "-c", "exec wtype -s 600000 2>/dev/null", (char *)NULL); _exit(127); }
	usleep(200000);

	CHECK((imd = wl_display_connect(NULL)));
	wl_registry_add_listener(imreg = wl_display_get_registry(imd), &im_reg_l, NULL);
	CHECK(wl_display_roundtrip(imd) >= 0 && imseat && imm && vpm);
	zwp_input_method_v2_add_listener(im = zwp_input_method_manager_v2_get_input_method(imm, imseat), &im_l, NULL);
	wl_display_flush(imd);

	CHECK(!backend_wl.open() && !backend_wl.win_open(0));
	int k0 = nkeys;
	draw();                        /* sway sizes a tiled window once it is mapped */
	pump(100);
	CHECK(count(KEY_RESIZE, k0) == 1);
	size_is(1280, 800, 120);        /* the whole output */
	draw();

	/* only changed rows are drawn, and moved rows copied: after random
	   edits (characters, ligatures, wide and overflowing symbols, long
	   clusters, attrs,
	   faces, the cursor) and scrolls over many frames into both buffers, the
	   screen is what a whole redraw gives, and not what it was before */
	cmd("grim -t ppm %s/0.ppm # %s", dir);
	static const char *const ch[] = { "a", "Z", "~", "λ", "é", " ", "#", "-", ">", "⟹", "😀", "👨‍👩‍👧", "e\xcc\x81\xcc\xa3" };   /* the last two longer than 4 bytes: kept in grid.c's cluster store */
	for (int f = 0; f < 75; f++) {   /* ends after cursor-only frames */
		/* edits in 10 frames of each 20 (the first over every row, so
		   rows differ), then 5 frames that move only the cursor and 5 that
		   only scroll, so a buffer's rows, two frames old, are found moved */
		int phase = f % 20;
		for (int i = 0; i < (phase ? 30 : 20 * g.rows) && phase < 10; i++) {
			int n = (int)rnd(13), r = phase ? (int)rnd(16) : i / 20;
			grid_put(&g, r, phase ? (int)rnd(20) : i % 20, (const unsigned char *)ch[n], strlen(ch[n]), n == 10 || n == 11 ? 2 : 1, (int)rnd(3));
			g.c[r * g.cols + rnd(20)].face = (unsigned short)rnd(FACE_COUNT);
			g.c[r * g.cols + rnd(20)].bg = (unsigned char)(rnd(2) ? rnd(FACE_COUNT) : 0);
		}
		if (phase >= 15) {         /* a scroll by k rows, either way */
			int k = (int)rnd(7) - 3, a = k > 0 ? k : -k;
			memmove(&g.c[(k < 0 ? a : 0) * g.cols], &g.c[(k > 0 ? a : 0) * g.cols], sizeof *g.c * (size_t)((g.rows - a) * g.cols));
		} else {
			g.crow = (int)rnd((unsigned)g.rows);
			g.ccol = (int)rnd(20);
		}
		show();
	}
	cmd("grim -t ppm %s/1.ppm # %s", dir);
	grid_free(&shown);              /* as after a suspend: redrawn whole */
	show();
	cmd("grim -t ppm %s/2.ppm && cmp -s %s/1.ppm %s/2.ppm && ! cmp -s %s/0.ppm %s/1.ppm", dir);

	/* a row back as it was two frames ago is drawn already in the buffer
	   drawn into, but the screen shows the other buffer's row: it still
	   needs its damage (the review of package 8, finding 1) */
	for (int i = 0; i < 4; i++) {
		grid_puts(&g, 3, 0, g.cols, "two frames ago", 14, ATTR_NONE, FACE_NONE);
		show();
		grid_puts(&g, 3, 0, g.cols, "the last frame", 14, ATTR_NONE, FACE_NONE);
		show();
		grid_puts(&g, 3, 0, g.cols, "two frames ago", 14, ATTR_NONE, FACE_NONE);
		show();
		cmd("grim -t ppm %s/1.ppm # %s", dir);
		grid_free(&shown);
		show();
		cmd("grim -t ppm %s/2.ppm && cmp -s %s/1.ppm %s/2.ppm", dir);
	}

	/* a row changed to or from a glyph that reaches left (↔ starts left of
	   its cell; " /\ " joins only with the blank before it, which a
	   partial redraw starts after) looks as a whole redraw draws it (the
	   review of PR 68); each old row is presented three times, so both
	   buffers hold it */
	static const char *const lig[][2] = { { " ↔", " a" }, { " a", " ↔" }, { " ⇔ b", " a b" }, { "x ↔", "x a" }, { "x /\\", "x /\\ y" } };
	for (int i = 0; i < 5; i++) {
		char a[64], b[64];
		snprintf(a, sizeof a, "%-24s", lig[i][0]);
		snprintf(b, sizeof b, "%-24s", lig[i][1]);
		grid_puts(&g, 3, 0, g.cols, a, strlen(a), ATTR_NONE, FACE_NONE);
		for (int k = 0; k < 3; k++) show();
		grid_puts(&g, 3, 0, g.cols, b, strlen(b), ATTR_NONE, FACE_NONE);
		show();
		cmd("grim -t ppm %s/1.ppm # %s", dir);
		grid_free(&shown);
		show();
		cmd("grim -t ppm %s/2.ppm && cmp -s %s/1.ppm %s/2.ppm", dir);
	}

	/* a symbol wider than its cell (⟹; 73 of Mathlib's 295 are) spills into
	   the space after it rather than being cut at its cell */
	grid_clear(&g);
	grid_put(&g, 0, 0, (const unsigned char *)"⟹", 3, 1, ATTR_NONE);
	g.crow = 5;
	show();
	CHECK(ink(cellw(), 0, cellw(), cellh(), 0));
	/* but not into a cell that has a character: there it is cut, so the
	   next cell looks the same with or without ⟹ before it */
	char b[512];
	grid_put(&g, 0, 1, (const unsigned char *)"a", 1, 1, ATTR_NONE);
	show();
	snprintf(b, sizeof b, "grim -g '%d,0 %dx%d' -t ppm %s/x.ppm", cellw(), cellw(), cellh(), dir);
	run(b);
	grid_put(&g, 0, 0, (const unsigned char *)" ", 1, 1, ATTR_NONE);
	show();
	snprintf(b, sizeof b, "grim -g '%d,0 %dx%d' -t ppm %s/y.ppm && cmp -s %s/x.ppm %s/y.ppm", cellw(), cellw(), cellh(), dir, dir, dir);
	run(b);

	/* the background layer (the cursor line) under a cell with no background of its own */
	grid_put(&g, 0, 2, (const unsigned char *)" ", 1, 1, ATTR_NONE);
	g.c[2].bg = FACE_CURSOR_LINE;
	show();
	CHECK(ink(2 * cellw(), 0, cellw(), cellh(), 0) == cellw() * cellh());
	/* an indent guide is not the preedit: no underline (guides are drawn from round B) */
	grid_put(&g, 0, 3, (const unsigned char *)" ", 1, 1, ATTR_GUIDE);
	show();
	CHECK(!ink(3 * cellw(), 0, cellw(), cellh(), 0));
	/* a diagnostic's undercurl under a blank: one red pixel a column, a wave
	   (not all on the last row) */
	grid_put(&g, 0, 4, (const unsigned char *)" ", 1, 1, ATTR_NONE);
	g.c[4].ul = FACE_DIAG_UNDER_ERROR;
	show();
	CHECK(ink(4 * cellw(), 0, cellw(), cellh(), 0) == cellw() && ink(4 * cellw(), 0, cellw(), cellh(), 1) == cellw());
	CHECK(ink(4 * cellw(), cellh() - 1, cellw(), 1, 0) < cellw());
	/* an image (package 21, thread 0's, agreed): Backend.cell is the font's
	   cell; tests/test_image.png (32 x 16, red then blue) fills its pixels
	   from row 2, each tile its own columns, the background past it; its
	   cells redrawn as text take it away */
	{
		int w, h, n;
		backend_wl.cell(0, &w, &h);
		CHECK(w == cellw() && h == cellh() && 4 * w > 32 && h > 16);
		CHECK((n = image_load("tests/test_image.png", 4, 1, w, h, 0x1e1e2e)) > 0);
		image_put(&g, 2, 0, n, 0);
		show();
		CHECK(ink(0, 2 * h, 32, 16, 0) == 512 && ink(0, 2 * h, 16, 16, 1) == 256 && !ink(16, 2 * h, 16, 16, 1));
		CHECK(!ink(32, 2 * h, 4 * w - 32, h, 0) && !ink(0, 2 * h + 16, 4 * w, h - 16, 0));
		grid_puts(&g, 2, 0, 4, "    ", 4, ATTR_NONE, FACE_NONE);
		show();
		CHECK(!ink(0, 2 * h, 4 * w, h, 0));
	}

	/* a cluster longer than a cell's 4 bytes is drawn from grid.c's store:
	   a thumbs up with a skin tone (8 bytes) is one brown glyph */
	grid_put(&g, 0, 0, (const unsigned char *)"👍🏽", 8, 2, ATTR_NONE);
	show();
	CHECK(ink(0, 0, 2 * cellw(), cellh(), 1) > cellw() * cellh() / 4);

	/* a ligature's glyph sits on its last cell and reaches back over the
	   others ("->"; " /\ " in JetBrains Mono): the first cell has its ink */
	grid_clear(&g);
	grid_puts(&g, 0, 0, g.cols, "x /\\ y ->", 9, ATTR_NONE, FACE_NONE);
	show();
	CHECK(ink(2 * cellw(), 0, cellw(), cellh(), 0) > cellh() / 2);
	CHECK(ink(7 * cellw(), 0, cellw(), cellh(), 0) > cellw() / 2);
	/* each part of a ligature ("-->", its first cell reversed) in its cell's
	   colours, as kitty's, and a spill (⟹) and a wide character's right half
	   in the symbol's, as foot's: every cell has ink, as much wherever the
	   cursor is */
	static const char *const pc[] = { "-->", "⟹ ", "中" };
	for (int i = 0; i < 3; i++) {
		int base[3];
		grid_clear(&g);
		grid_puts(&g, 0, 0, g.cols, pc[i], strlen(pc[i]), ATTR_NONE, FACE_NONE);
		if (!i) g.c[0].attr = ATTR_REV;
		for (int cur = -1; cur < 3 - !!i; cur++) {
			g.crow = cur < 0 ? 5 : 0;
			g.ccol = cur < 0 ? 0 : cur;
			show();
			for (int k = 0; k < 3 - !!i; k++) {
				int n = ink(k * cellw(), 0, cellw(), cellh(), 2);
				if (cur < 0) base[k] = n;
				CHECK(n && n == base[k]);
			}
		}
	}
	g.crow = 5;

	/* keys, with the Control transformation and the keys keys.h names */
	k0 = nkeys;
	run("LC_ALL=C.UTF-8 wtype aé -k Escape -k BackSpace -M ctrl r -m ctrl -k Up -k Return -k Tab -M shift -k Tab -m shift -k ISO_Left_Tab -M ctrl -k period -m ctrl -k period");
	pump(50);
	int want[] = { 'a', 0xE9, KEY_ESC, KEY_BS, KEY_CTRL_R, KEY_UP, KEY_ENTER, KEY_TAB, KEY_S_TAB, KEY_S_TAB, KEY_CTRL_DOT, '.' };
	if (nkeys - k0 != 12)
		for (int i = k0; i < nkeys; i++) fprintf(stderr, "key %d: %#x\n", i - k0, (unsigned)keys[i]);
	CHECK(nkeys - k0 == 12);
	for (int i = 0; i < 12; i++) CHECK(keys[k0 + i] == want[i]);

	/* keys that arrive together but are taken 4 at a time: those left in the
	   backend's queue make an fd readable, as main.c reads only then */
	CHECK(!system("wtype -d 1 abcdefgh"));   /* not pumped: all wait in the socket */
	usleep(300000);
	k0 = nkeys;
	for (int i = 0, fds[2]; i < 6; i++) {
		backend_wl.fds(fds);
		struct pollfd p[2] = { { fds[0], POLLIN, 0 }, { fds[1], POLLIN, 0 } };
		if (poll(p, 2, 500) > 0) nkeys += backend_wl.input(keys + nkeys, wins + nkeys, 4);
	}
	CHECK(count('a', k0) == 1 && count('h', k0) == 1 && nkeys - k0 == 8);

	/* repeat: delay 300 ms, 25/s, key held 1000 ms: 1 + 18 by the clock */
	k0 = nkeys;
	run("wtype -P x -s 1000 -p x");
	int held = count('x', k0);
	pump(300);
	fprintf(stderr, "repeat: %d x for a 1000 ms hold\n", held);
	CHECK(held >= 16 && held <= 21);
	CHECK(count('x', k0) == held);   /* nothing after the release */

	/* clipboard: ours read by another client, then another client's read by us */
	CHECK(!backend_wl.clip_set("from homeros\n", 13));
	cmd("wl-paste -n > %s/p && printf 'from homeros\\n' | cmp - %s/p", dir);
	char s[16], *s4 = malloc(4);   /* exactly 4, so ASan sees a byte past max */
	CHECK(backend_wl.clip_get(s4, 4) == 13 && !memcmp(s4, "from", 4));   /* ours, read back */
	/* a reader of ours that stalls 2 s does not stop the editor, and gets
	   all of 1 MB (the review of package 8, finding 3) */
	char *big = malloc(1 << 20);
	memset(big, 'x', 1 << 20);
	CHECK(!backend_wl.clip_set(big, 1 << 20));
	free(big);
	char rd[128];                   /* started unpumped, so the wait is timed below */
	snprintf(rd, sizeof rd, "(wl-paste -n | (sleep 2; wc -c > %s/n)) &", dir);
	CHECK(!system(rd));
	double worst = 0;
	for (double end = now() + 3000; now() < end;) {
		double a = now();
		pump(10);
		if (now() - a > worst) worst = now() - a;
	}
	CHECK(worst < 200);
	cmd("test $(cat %s/n) = 1048576 # %s", dir);
	run("printf 'from outside' | wl-copy");
	pump(100);
	CHECK(backend_wl.clip_get(s4, 4) == 12 && !memcmp(s4, "from", 4));   /* too little room */
	CHECK(backend_wl.clip_get(s, sizeof s) == 12 && !memcmp(s, "from outside", 12));
	free(s4);

	/* input method: enabled on focus; preedit asks for a redraw, commit gives keys */
	pump(100);
	CHECK(im_active);
	zwp_input_method_v2_set_preedit_string(im, "ab", 2, 2);
	zwp_input_method_v2_commit(im, (uint32_t)im_dones);
	k0 = nkeys;
	pump(100);
	CHECK(count(KEY_RESIZE, k0) == 1);
	draw();
	zwp_input_method_v2_commit_string(im, "üñ");
	zwp_input_method_v2_commit(im, (uint32_t)im_dones);
	k0 = nkeys;
	pump(100);
	CHECK(nkeys - k0 == 3 && keys[k0] == 0xFC && keys[k0 + 1] == 0xF1 && keys[k0 + 2] == KEY_RESIZE);
	draw();

	/* pointer: the window sets the text cursor.  A screenshot with the
	   pointer away and one with it at (310, 310) differ where the cursor is
	   drawn; the I-beam's hotspot is its middle, so it reaches above the
	   pointer, which the default arrow (hotspot at its tip) never does. */
	struct zwlr_virtual_pointer_v1 *vp = zwlr_virtual_pointer_manager_v1_create_virtual_pointer(vpm, imseat);
	pump(100);
	zwlr_virtual_pointer_v1_motion_absolute(vp, 0, 700, 600, 1280, 800);
	zwlr_virtual_pointer_v1_frame(vp);
	pump(100);
	cmd("cd %s && grim -c -t ppm -g '290,290 40x40' a.ppm # %s", dir);
	zwlr_virtual_pointer_v1_motion_absolute(vp, 0, 310, 310, 1280, 800);
	zwlr_virtual_pointer_v1_frame(vp);
	pump(100);
	cmd("cd %s && grim -c -t ppm -g '290,290 40x40' b.ppm # %s", dir);
	CHECK(above(16));

	/* a left click, a wheel notch each way and a touchpad's 3 rows are keys
	   at the pointer's cell */
	int mr, mc;
	zwlr_virtual_pointer_v1_motion_absolute(vp, 0, 500, 300, 1280, 800);
	k0 = nkeys;
	zwlr_virtual_pointer_v1_button(vp, 0, BTN_LEFT, WL_POINTER_BUTTON_STATE_PRESSED);
	zwlr_virtual_pointer_v1_button(vp, 0, BTN_LEFT, WL_POINTER_BUTTON_STATE_RELEASED);
	zwlr_virtual_pointer_v1_frame(vp);
	zwlr_virtual_pointer_v1_axis_source(vp, WL_POINTER_AXIS_SOURCE_WHEEL);
	zwlr_virtual_pointer_v1_axis_discrete(vp, 0, WL_POINTER_AXIS_VERTICAL_SCROLL, wl_fixed_from_int(15), 1);
	zwlr_virtual_pointer_v1_frame(vp);
	zwlr_virtual_pointer_v1_axis_source(vp, WL_POINTER_AXIS_SOURCE_WHEEL);
	zwlr_virtual_pointer_v1_axis_discrete(vp, 0, WL_POINTER_AXIS_VERTICAL_SCROLL, wl_fixed_from_int(-15), -1);
	zwlr_virtual_pointer_v1_frame(vp);
	for (int i = 0; i < 2; i++) {   /* half of 3 rows twice */
		zwlr_virtual_pointer_v1_axis_source(vp, WL_POINTER_AXIS_SOURCE_FINGER);
		zwlr_virtual_pointer_v1_axis(vp, 0, WL_POINTER_AXIS_VERTICAL_SCROLL, wl_fixed_from_double(1.5 * cellh()));
		zwlr_virtual_pointer_v1_frame(vp);
	}
	pump(100);
	if (nkeys - k0 != 4)
		for (int i = k0; i < nkeys; i++) fprintf(stderr, "key %d: %#x\n", i - k0, (unsigned)keys[i]);
	CHECK(nkeys - k0 == 4 && keys[k0] == KEY_MOUSE_LEFT && keys[k0 + 1] == KEY_WHEEL_DOWN
	      && keys[k0 + 2] == KEY_WHEEL_UP && keys[k0 + 3] == KEY_WHEEL_DOWN);
	backend_wl.mouse(&mr, &mc);
	CHECK(mr == 300 / cellh() && mc == 500 / cellw());
	zwlr_virtual_pointer_v1_destroy(vp);

	/* configure size: floating, resized by the compositor */
	k0 = nkeys;
	run(SWAYMSG "floating enable, resize set 640 480");
	pump(100);
	CHECK(count(KEY_RESIZE, k0) >= 1);
	size_is(640, 480, 120);
	draw();

	/* scale: 1.5 gives a 960x720 buffer and a 24 px font */
	int ch1 = cellh();
	k0 = nkeys;
	run(SWAYMSG "output HEADLESS-1 scale 1.5");
	pump(200);
	CHECK(count(KEY_RESIZE, k0) >= 1 && cellh() > ch1);
	size_is(640, 480, 180);
	draw();
	run(SWAYMSG "output HEADLESS-1 scale 1");
	pump(200);
	size_is(640, 480, 120);
	CHECK(cellh() == ch1);
	draw();

	/* frames: a second window (tab page 1), mapped by its first frame,
	   gets the keyboard: a focus change, and its keys say so.  Its frame
	   callback is its own (Frames.tla) */
	k0 = nkeys;
	CHECK(!backend_wl.win_open(1));
	int cw0, ch0, cw1, ch1b, r1, c1;
	Grid g1 = { 0 }, shown1 = { 0 };
	backend_wl.size(1, &r1, &c1);
	CHECK(!grid_resize(&g1, r1, c1));
	grid_puts(&g1, 0, 0, c1, "frame 1", 7, ATTR_NONE, FACE_NONE);
	CHECK(backend_wl.ready(1) && backend_wl.ready(0));
	backend_wl.present(1, &g1, &shown1);
	CHECK(!backend_wl.ready(1) && backend_wl.ready(0));
	pump(300);
	int f1 = -1;
	for (int i = k0; i < nkeys; i++) if (keys[i] == KEY_FOCUS) f1 = wins[i];
	CHECK(count(KEY_FOCUS, k0) == 1 && f1 == 1);
	k0 = nkeys;
	run("wtype -s 200 x");
	/* wtype's new keyboard leaves and enters window 1 again: no change */
	CHECK(keys[nkeys - 1] == 'x' && wins[nkeys - 1] == 1 && !count(KEY_FOCUS, k0));
	/* on an output at scale 1.5 its cells are bigger than window 0's,
	   and each window draws at its own font size */
	run(SWAYMSG "create_output, output HEADLESS-2 resolution 1280x800 scale 1.5, move container to output HEADLESS-2, focus output HEADLESS-2");
	pump(300);
	backend_wl.cell(1, &cw1, &ch1b);
	backend_wl.cell(0, &cw0, &ch0);
	backend_wl.size(1, &r1, &c1);     /* in window 1's cells, whichever font was used last */
	CHECK(ch1b > ch0 && cw1 > cw0 && r1 > 1 && c1 > 1);
	CHECK(!grid_resize(&g1, r1, c1));
	for (double end = now() + 2000; !backend_wl.ready(1) && now() < end;) pump(5);
	backend_wl.present(1, &g1, &shown1);
	CHECK(shown1.rows == r1);
	draw();
	backend_wl.cell(1, &cw1, &ch1b);
	show();
	int w_, h_, b_;
	font_cell(&w_, &h_, &b_);
	CHECK(h_ == ch0);                 /* window 0 drawn with its own font, after window 1's was used */
	/* SPC 5 o: raise window 0; its focus comes before KEY_RAISED (Focus.tla) */
	k0 = nkeys;
	backend_wl.raise(0);
	for (double end = now() + 2000; !count(KEY_RAISED, k0) && now() < end;) pump(5);
	int f0 = -1, r0 = -1;
	for (int i = k0; i < nkeys; i++) {
		if (keys[i] == KEY_FOCUS && wins[i] == 0 && f0 < 0) f0 = i;
		if (keys[i] == KEY_RAISED && wins[i] == -1) r0 = i;
	}
	CHECK(f0 >= 0 && r0 > f0 && count(KEY_RAISED, k0) == 1);
	run("wtype -s 200 y");
	CHECK(keys[nkeys - 1] == 'y' && wins[nkeys - 1] == 0);
	/* closing window 1 leaves window 0 drawing */
	backend_wl.win_close(1);
	pump(100);
	draw();
	grid_free(&g1);
	grid_free(&shown1);
	run(SWAYMSG "output HEADLESS-2 disable");

	/* hidden: no frame callback, so nothing is drawn until shown again */
	run(SWAYMSG "move scratchpad");
	pump(100);
	grid_puts(&g, 1, 0, g.cols, "hidden", 6, ATTR_NONE, FACE_NONE);
	CHECK(backend_wl.ready(0));
	backend_wl.present(0, &g, &shown);
	pump(500);
	CHECK(!backend_wl.ready(0));
	run(SWAYMSG "scratchpad show");
	pump(300);
	CHECK(backend_wl.ready(0));

	/* closing the window is a key; the window stays until the editor quits */
	k0 = nkeys;
	run(SWAYMSG "kill");
	pump(100);
	CHECK(count(KEY_CLOSE, k0) == 1);

	/* the compositor gone: input() says so with -1 (backend.h), not a key
	   (the review of package 8, finding 4); the input method's connection
	   dies too, so not pump */
	kill(-cpid, SIGKILL);
	waitpid(cpid, NULL, 0);
	{
		int fds[2];
		backend_wl.fds(fds);
		struct pollfd p = { fds[0], POLLIN, 0 };
		CHECK(poll(&p, 1, 1000) > 0);
		CHECK(backend_wl.input(keys + nkeys, wins + nkeys, 64) == -1);
	}

	backend_wl.close();
	zwp_input_method_v2_destroy(im);
	zwp_input_method_manager_v2_destroy(imm);
	zwlr_virtual_pointer_manager_v1_destroy(vpm);
	wl_registry_destroy(imreg);
	wl_seat_destroy(imseat);
	wl_display_disconnect(imd);
	imd = NULL;
	stop();
	waitpid(kb, NULL, 0);
}

static void test_weston(void)
{
	start("exec weston --backend=headless --renderer=pixman --shell=kiosk-shell.so --no-config "
	      "--width=1000 --height=700 --idle-time=0 --socket=wayland-w >%s/log 2>&1 # %s");
	CHECK(!backend_wl.open() && !backend_wl.win_open(0));
	size_is(1000, 700, 120);
	draw();
	draw();
	backend_wl.close();
	stop();
}

int main(void)
{
	signal(SIGPIPE, SIG_IGN);
	atexit(stop);
	test_weston();
	test_sway();
	grid_free(&g);
	grid_free(&shown);
	return 0;
}
