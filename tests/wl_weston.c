/* The Wayland backend under a headless weston 16 with its test plugin,
   which types, clicks and scrolls through a seat of its own.  Each scenario
   runs in its own process with its own weston.  Run by `make wl-test`
   (src/wl/build.mk) with WESTON set to the weston it builds.

   The same binary is also the input method weston starts (WL_TEST_IM), and
   the other client of the clipboard tests. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>
#include "backend.h"
#include "font.h"
#include "keys.h"
#include "check.h"
#include "weston-test.h"
#include "weston-output-capture.h"
#include "input-method-unstable-v1.h"
#include "xdg-shell.h"

static const Backend *b = &backend_wl;
static struct wl_display *drv;
static struct weston_test *wt;
static struct wl_output *wout;
static struct wl_shm *wshm;
static struct weston_capture_v1 *wcap;
static char sock[64], self[4096];
static pid_t wpid;
static int gl;   /* weston runs its GL renderer */
static Grid g, shown;
static int dirty, presents;

static long long now(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000000000LL + t.tv_nsec;
}

static void drv_reg(void *d, struct wl_registry *r, uint32_t id, const char *i, uint32_t v)
{
	(void)d; (void)v;
	if (!strcmp(i, "weston_test")) wt = wl_registry_bind(r, id, &weston_test_interface, 1);
	else if (!strcmp(i, "wl_output") && !wout) wout = wl_registry_bind(r, id, &wl_output_interface, 1);
	else if (!strcmp(i, "wl_shm")) wshm = wl_registry_bind(r, id, &wl_shm_interface, 1);
	else if (!strcmp(i, "weston_capture_v1")) wcap = wl_registry_bind(r, id, &weston_capture_v1_interface, 1);
}
static void drv_rm(void *d, struct wl_registry *r, uint32_t id) { (void)d; (void)r; (void)id; }
static const struct wl_registry_listener drv_l = { drv_reg, drv_rm };

static void start(const char *shell, int w, int h, int scale)
{
	char wd[4096];
	CHECK(getenv("WESTON") && realpath(getenv("WESTON"), wd));
	char dir[] = "/tmp/homeros-wl-XXXXXX", ini[128], arg[8][4200];
	CHECK(mkdtemp(dir));
	if (!getenv("XDG_RUNTIME_DIR")) setenv("XDG_RUNTIME_DIR", dir, 1);
	snprintf(sock, sizeof sock, "homeros-%d", getpid());
	snprintf(ini, sizeof ini, "%s/weston.ini", dir);
	FILE *f = fopen(ini, "w");
	CHECK(f);
	fprintf(f, "[keyboard]\nrepeat-rate=50\nrepeat-delay=200\n[input-method]\npath=%s\n[shell]\npanel-position=none\nanimation=none\nstartup-animation=none\nfocus-animation=none\n", self);
	fclose(f);
	snprintf(arg[0], sizeof arg[0], "%s/bin/weston", wd);
	snprintf(arg[1], sizeof arg[1], "--shell=%s", shell);
	snprintf(arg[2], sizeof arg[2], "--socket=%s", sock);
	snprintf(arg[3], sizeof arg[3], "--modules=%s/lib/weston/test-plugin.so", wd);
	snprintf(arg[4], sizeof arg[4], "--width=%d", w);
	snprintf(arg[5], sizeof arg[5], "--height=%d", h);
	snprintf(arg[6], sizeof arg[6], "--scale=%d", scale);
	snprintf(arg[7], sizeof arg[7], "--config=%s", ini);
	/* WL_RENDERER=gl runs the tests on weston's GL renderer (Mesa's
	   llvmpipe), which gives a buffer back at commit, as Hyprland does;
	   pixman holds it until the next one is shown */
	const char *rend = getenv("WL_RENDERER") ? getenv("WL_RENDERER") : "pixman";
	gl = !strcmp(rend, "gl");
	char renderer[64], egl[4200];
	snprintf(renderer, sizeof renderer, "--renderer=%s", rend);
	snprintf(egl, sizeof egl, "%s/share/egl-vendor.json", wd);
	if (!(wpid = fork())) {
		setenv("WL_TEST_IM", "1", 1);
		setenv("__EGL_VENDOR_LIBRARY_FILENAMES", egl, 1);
		char log[160];
		snprintf(log, sizeof log, "%s/weston.log", dir);
		int fd = open(log, O_WRONLY | O_CREAT | O_TRUNC, 0600);
		dup2(fd, 1);
		dup2(fd, 2);
		execl(arg[0], arg[0], "--backend=headless", renderer, "--idle-time=0", "--debug", arg[1], arg[2],
		      arg[3], arg[4], arg[5], arg[6], arg[7], (char *)NULL);
		_exit(127);
	}
	setenv("WAYLAND_DISPLAY", sock, 1);
	for (int i = 0; i < 500 && !(drv = wl_display_connect(NULL)); i++) usleep(10000);
	CHECK(drv);
	wl_registry_add_listener(wl_display_get_registry(drv), &drv_l, NULL);
	wl_display_roundtrip(drv);
	CHECK(wt);
}

static void stop(void)
{
	b->close();
	wl_display_disconnect(drv);
	kill(wpid, SIGTERM);
	waitpid(wpid, NULL, 0);
}

static void ts(uint32_t *hi, uint32_t *lo, uint32_t *ns)
{
	long long t = now();
	*hi = 0;
	*lo = (uint32_t)(t / 1000000000);
	*ns = (uint32_t)(t % 1000000000);
}

static void key(uint32_t k, int down)
{
	uint32_t a, c, d;
	ts(&a, &c, &d);
	weston_test_send_key(wt, a, c, d, k, down);
	wl_display_flush(drv);
}

static void tap(uint32_t k) { key(k, 1); key(k, 0); }

/* The main loop of src/main.c in small: read keys, draw when dirty and
   the backend is ready.  Keys go to out; returns how many, or stops at
   want keys or after ms. */
static int pump(int ms, int *out, int want)
{
	int fds[2], n = b->fds(fds), got = 0;
	struct pollfd p[2] = { { fds[0], POLLIN, 0 }, { fds[1], POLLIN, 0 } };
	for (long long end = now() + ms * 1000000LL; now() < end && (!want || got < want);) {
		if (dirty && b->ready()) {
			for (int r = 0; r < g.rows; r++) grid_puts(&g, r, 0, "fn main() -> int { 0 }", r % 3);
			b->present(&g, &shown);
			presents++;
			dirty = 0;
		}
		if (poll(p, (nfds_t)n, (int)((end - now()) / 1000000) + 1) <= 0) continue;
		int k[256], m = b->input(k, 256);
		for (int i = 0; i < m; i++) {
			if (k[i] == KEY_RESIZE) {
				int rows, cols;
				b->size(&rows, &cols);
				CHECK(!grid_resize(&g, rows, cols));
			} else if (got < want) out[got++] = k[i];
			dirty = 1;
		}
	}
	return got;
}

static void open_editor(void)
{
	int rows, cols, k[8];
	CHECK(!b->open());
	b->size(&rows, &cols);
	CHECK(!grid_resize(&g, rows, cols));
	dirty = 1;
	pump(300, k, 8);   /* the first frame, and the output's scale */
}

static int other(int mode, const char *want, size_t n, int ctl);

/* ---- the screen, read back through weston's capture protocol ---- */

static int cap_w, cap_h, cap_state;
static uint32_t cap_fmt;
/* wl_shm's codes are DRM's, but for the two oldest formats */
static void cs_format(void *d, struct weston_capture_source_v1 *c, uint32_t f)
{
	(void)d; (void)c;
	cap_fmt = f == 0x34325241 ? WL_SHM_FORMAT_ARGB8888 : f == 0x34325258 ? WL_SHM_FORMAT_XRGB8888 : f;
}
static void cs_size(void *d, struct weston_capture_source_v1 *c, int32_t w, int32_t h) { (void)d; (void)c; cap_w = w; cap_h = h; }
static void cs_complete(void *d, struct weston_capture_source_v1 *c) { (void)d; (void)c; cap_state = 1; }
static void cs_retry(void *d, struct weston_capture_source_v1 *c) { (void)d; (void)c; cap_state = 2; }
static void cs_failed(void *d, struct weston_capture_source_v1 *c, const char *m) { (void)d; (void)c; fprintf(stderr, "capture: %s\n", m ? m : "?"); cap_state = 3; }
static const struct weston_capture_source_v1_listener cs_l = {
	.format = cs_format, .size = cs_size, .complete = cs_complete, .retry = cs_retry, .failed = cs_failed };

/* Waits until the last frame is on screen (its callback came) */
static void settle(void)
{
	int k[1];
	for (long long t = now(); !b->ready() && now() - t < 2000000000LL;) pump(5, k, 0);
	CHECK(b->ready());
	pump(50, k, 0);
}

/* The output's pixels.  Weston takes them at its next repaint, and the GL
   renderer repaints only what is damaged, so with kick the editor first
   shows one cell changed and then, after the request, the grid again: that
   repaint has damage and shows the grid as it was.  Meanwhile the editor's
   events keep being read.  Returns w * h pixels, malloc'd. */
static uint32_t *capture(int kick)
{
	Cell keep = g.c[0];
	int k[1];
	if (kick) {
		grid_put(&g, 0, 0, (const unsigned char *)"#", 1, 1, ATTR_REV);
		settle();
		b->present(&g, &shown);
		settle();
		g.c[0] = keep;
	}
	CHECK(wcap && wout && wshm);
	struct weston_capture_source_v1 *c = weston_capture_v1_create(wcap, wout, WESTON_CAPTURE_V1_SOURCE_FRAMEBUFFER);
	weston_capture_source_v1_add_listener(c, &cs_l, NULL);
	wl_display_roundtrip(drv);
	CHECK(cap_w > 0 && cap_h > 0);
	size_t sz = (size_t)cap_w * (size_t)cap_h * 4;
	int fd = memfd_create("cap", 0);
	CHECK(fd >= 0 && !ftruncate(fd, (off_t)sz));
	uint32_t *px = mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	struct wl_shm_pool *pool = wl_shm_create_pool(wshm, fd, (int32_t)sz);
	struct wl_buffer *wb = wl_shm_pool_create_buffer(pool, 0, cap_w, cap_h, cap_w * 4, cap_fmt);
	do {
		cap_state = 0;
		weston_capture_source_v1_capture(c, wb);
		wl_display_flush(drv);
		if (kick) b->present(&g, &shown);
		struct pollfd p = { wl_display_get_fd(drv), POLLIN, 0 };
		for (long long t = now(); !cap_state && now() - t < 5000000000LL;) {
			if (poll(&p, 1, 1) > 0) CHECK(wl_display_dispatch(drv) >= 0);
			pump(1, k, 0);
		}
	} while (cap_state == 2);
	CHECK(cap_state == 1);
	uint32_t *copy = malloc(sz);
	for (size_t i = 0; i < sz / 4; i++) copy[i] = px[i] & 0xffffff;
	munmap(px, sz);
	close(fd);
	wl_buffer_destroy(wb);
	wl_shm_pool_destroy(pool);
	weston_capture_source_v1_destroy(c);
	return copy;
}


/* Frames drawn piece by piece (changed rows only, into two buffers that
   take turns) must leave the screen exactly as one full redraw of the last
   grid does. */
static void pixels(void)
{
	start("kiosk", 645, 480, 1);   /* 645: a margin right of the cells */
	open_editor();
	/* U+27F9 is twice a cell wide in JetBrains Mono: its ink must stay in
	   the cells */
	static const char *ch[] = { "a", "{", "-", ">", "\xc3\xa9", "\xe2\x88\x80", " ", "\xe2\x9f\xb9" };
	int k[1];
	for (int f = 0; f < 300; f++) {
		for (int i = rnd(6); i >= 0; i--) {
			int r = (int)rnd((unsigned)g.rows), c = (int)rnd((unsigned)g.cols);
			const char *s = ch[rnd(8)];
			grid_put(&g, r, c, (const unsigned char *)s, strlen(s), 1, (int)rnd(3));
			g.c[r * g.cols + c].face = (unsigned char)rnd(FACE_COUNT);
		}
		g.crow = (int)rnd((unsigned)g.rows);
		g.ccol = (int)rnd((unsigned)g.cols);
		while (!b->ready()) pump(2, k, 0);
		b->present(&g, &shown);
	}
	settle();
	/* the screen as the damage left it, then the whole last buffer (a
	   compositor may show all of it any time: Hyprland's blur, a window
	   moving away) */
	uint32_t *a = capture(gl);
	pid_t p = fork();
	if (!p) _exit(other(2, NULL, 0, -1));
	int st;
	while (waitpid(p, &st, WNOHANG) == 0) pump(10, k, 0);
	CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 0);
	settle();
	/* without a kick: its two frames would redraw what a stale buffer hides
	   (GL, which needs one, uses one buffer, so has none stale) */
	uint32_t *a2 = capture(gl);
	grid_free(&shown);   /* the contract: a shown grid of another size is redrawn whole */
	b->present(&g, &shown);
	settle();
	uint32_t *z = capture(gl);
	long differ = 0, differ2 = 0, ink = 0, cursor = 0;
	for (int i = 0; i < cap_w * cap_h; i++) {
		differ += a[i] != z[i];
		differ2 += a2[i] != z[i];
		ink += a[i] != 0x1e1e2e;
		cursor += a[i] == 0xf5e0dc;
	}
	printf("pixels: %ld and %ld differ of %d, %ld not background, %ld cursor\n", differ, differ2, cap_w * cap_h, ink, cursor);
	CHECK(differ == 0 && differ2 == 0 && ink > 10000 && cursor > 50);
	stop();
}

/* ---- scenarios ---- */

static void sized(int scale)
{
	start("kiosk", 1024, 768, scale);   /* weston takes the logical size */
	open_editor();
	int rows, cols, cw, ch, base;
	b->size(&rows, &cols);
	font_cell(&cw, &ch, &base);
	/* kiosk-shell configures the output's size: 1024 x 768 logical */
	CHECK(rows == 768 * scale / ch && cols == 1024 * scale / cw);
	CHECK(g.rows == rows && g.cols == cols);
	CHECK(ch >= 16 * scale);   /* the font follows the output scale */
	printf("scale %d: cell %dx%d px, %d x %d cells\n", scale, cw, ch, rows, cols);
	stop();
}

static void frames(void)
{
	start("kiosk", 1024, 768, 1);
	open_editor();
	/* frame callbacks: after a present the backend is not ready until the
	   compositor's callback, then it is */
	int k[2048];
	dirty = 1;
	pump(100, k, 1);
	int before = presents;
	dirty = 1;
	pump(1000, k, 0);
	CHECK(presents == before + 1);
	dirty = 1;
	CHECK(b->ready());
	for (int r = 0; r < g.rows; r++) grid_puts(&g, r, 0, "x", 0);
	b->present(&g, &shown);
	CHECK(!b->ready());
	long long t = now();
	while (!b->ready() && now() - t < 1000000000LL) pump(5, k, 0);
	CHECK(b->ready());
	printf("frame callback after %.1f ms\n", (now() - t) / 1e6);
	/* always something new to draw for 1 s: at most a frame per refresh
	   (weston's headless output runs at 60 Hz), none that is never shown */
	before = presents;
	for (t = now(); now() - t < 1000000000LL;) {
		dirty = 1;
		pump(1, k, 0);
	}
	printf("1 s of redraws: %d frames\n", presents - before);
	CHECK(presents - before <= 62);
	stop();
}

static void keys(void)
{
	start("kiosk", 1024, 768, 1);
	open_editor();
	int k[256], n;
	/* a, shift-a, ctrl-r, backspace, enter, escape, up, delete (no code) */
	tap(30);
	key(42, 1); tap(30); key(42, 0);
	key(29, 1); tap(19); key(29, 0);
	tap(14); tap(28); tap(1); tap(103); tap(111); tap(48);
	n = pump(1000, k, 9);
	int want[] = { 'a', 'A', KEY_CTRL_R, KEY_BS, KEY_ENTER, KEY_ESC, KEY_UP, 'b' };
	CHECK(n == 8);
	for (int i = 0; i < 8; i++) CHECK(k[i] == want[i]);

	/* keys the caller had no room for wake it again: read one at a time,
	   and only when poll says so */
	for (int i = 0; i < 5; i++) tap(30);
	usleep(100000);
	int fd2[2], got = 0;
	b->fds(fd2);
	struct pollfd q[2] = { { fd2[0], POLLIN, 0 }, { fd2[1], POLLIN, 0 } };
	while (got < 5 && poll(q, 2, 300) > 0) got += b->input(k + got, 1);
	CHECK(got == 5);

	/* repeat: rate 50, delay 200 ms (weston.ini); held 600 ms is the key and
	   20 repeats */
	long long t = now();
	key(36, 1);
	n = pump(600 - (int)((now() - t) / 1000000), k, 256);
	key(36, 0);
	n += pump(100, k + n, 256 - n);
	printf("held j 600 ms: %d keys\n", n);
	CHECK(n >= 19 && n <= 23);
	for (int i = 0; i < n; i++) CHECK(k[i] == 'j');
	/* and then nothing wakes the editor: 0 wake-ups in 2 s */
	int fds[2];
	b->fds(fds);
	struct pollfd p[2] = { { fds[0], POLLIN, 0 }, { fds[1], POLLIN, 0 } };
	CHECK(poll(p, 2, 2000) == 0);

	/* pointer: a click at the middle of cell (3, 5), wheel notches */
	int cw, ch, base, r, c;
	font_cell(&cw, &ch, &base);
	uint32_t a, s, ns;
	ts(&a, &s, &ns);
	weston_test_move_pointer(wt, a, s, ns, 5 * cw + cw / 2, 3 * ch + ch / 2);
	weston_test_send_button(wt, a, s, ns, 0x110, 1);
	weston_test_send_button(wt, a, s, ns, 0x110, 0);
	weston_test_send_axis(wt, a, s, ns, 0, wl_fixed_from_int(15));
	weston_test_send_axis(wt, a, s, ns, 0, wl_fixed_from_int(-15));
	for (int i = 0; i < 3; i++) weston_test_send_axis(wt, a, s, ns, 0, wl_fixed_from_int(5));
	wl_display_flush(drv);
	n = pump(1000, k, 4);
	CHECK(n == 4 && k[0] == KEY_MOUSE_LEFT && k[1] == KEY_WHEEL_DOWN && k[2] == KEY_WHEEL_UP && k[3] == KEY_WHEEL_DOWN);
	b->mouse(&r, &c);
	CHECK(r == 3 && c == 5);
	stop();
}

/* The compositor goes away: the editor quits without writing. */
static void gone(void)
{
	start("kiosk", 640, 480, 1);
	open_editor();
	kill(wpid, SIGKILL);
	waitpid(wpid, NULL, 0);
	int k[8];
	CHECK(pump(1000, k, 8) == 6);
	CHECK(k[0] == KEY_ESC && k[1] == ':' && k[2] == 'q' && k[3] == 'a' && k[4] == '!' && k[5] == KEY_ENTER);
	b->close();
	wl_display_disconnect(drv);
}

/* The editor itself (src/main.c) in a window: it opens a file, takes typed
   keys, saves and quits. */
static void editor(void)
{
	start("kiosk", 640, 480, 1);
	char path[] = "/tmp/homeros-wl-file-XXXXXX";
	int fd = mkstemp(path);
	CHECK(fd >= 0 && write(fd, "abc\n", 4) == 4);
	close(fd);
	pid_t p = fork();
	if (!p) {
		execl(getenv("HOMEROS"), "homeros", path, (char *)NULL);
		_exit(127);
	}
	usleep(500000);   /* its window maps and takes the focus */
	uint32_t keys_in[] = { 23, 35, 23, 1 };
	for (int i = 0; i < 4; i++) tap(keys_in[i]);
	key(42, 1); tap(39); key(42, 0);   /* : */
	tap(17); tap(16); tap(28);
	int st = -1;
	for (int i = 0; i < 300 && waitpid(p, &st, WNOHANG) == 0; i++) usleep(10000);
	if (st == -1) { kill(p, SIGKILL); waitpid(p, &st, 0); }
	char got[16] = { 0 };
	fd = open(path, O_RDONLY);
	CHECK(fd >= 0 && read(fd, got, sizeof got - 1) >= 0);
	close(fd);
	unlink(path);
	CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 0);
	CHECK(!strcmp(got, "hiabc\n"));
	wl_display_disconnect(drv);
	kill(wpid, SIGTERM);
	waitpid(wpid, NULL, 0);
}

/* ---- the other client of the clipboard tests ---- */

static struct wl_compositor *oc;
static struct wl_shm *os;
static struct xdg_wm_base *ow;
static struct wl_seat *oseat;
static struct wl_data_device_manager *oddm;
static uint32_t oserial;
static int ofocus;
static struct wl_data_offer *osel;
static const char *odata;
static size_t on;

static void o_reg(void *d, struct wl_registry *r, uint32_t id, const char *i, uint32_t v)
{
	(void)d; (void)v;
	if (!strcmp(i, "wl_compositor")) oc = wl_registry_bind(r, id, &wl_compositor_interface, 4);
	else if (!strcmp(i, "wl_shm")) os = wl_registry_bind(r, id, &wl_shm_interface, 1);
	else if (!strcmp(i, "xdg_wm_base")) ow = wl_registry_bind(r, id, &xdg_wm_base_interface, 1);
	else if (!strcmp(i, "wl_seat") && !oseat) oseat = wl_registry_bind(r, id, &wl_seat_interface, 4);
	else if (!strcmp(i, "wl_data_device_manager")) oddm = wl_registry_bind(r, id, &wl_data_device_manager_interface, 1);
}
static const struct wl_registry_listener o_reg_l = { o_reg, drv_rm };
static void o_conf(void *d, struct xdg_surface *x, uint32_t s) { (void)d; xdg_surface_ack_configure(x, s); }
static const struct xdg_surface_listener o_xs_l = { o_conf };
static void o_kmap(void *d, struct wl_keyboard *k, uint32_t f, int fd, uint32_t n) { (void)d; (void)k; (void)f; (void)n; close(fd); }
static void o_kenter(void *d, struct wl_keyboard *k, uint32_t s, struct wl_surface *f, struct wl_array *a)
{ (void)d; (void)k; (void)f; (void)a; oserial = s; ofocus = 1; }
static void o_kleave(void *d, struct wl_keyboard *k, uint32_t s, struct wl_surface *f) { (void)d; (void)k; (void)s; (void)f; ofocus = 0; }
static void o_kkey(void *d, struct wl_keyboard *k, uint32_t s, uint32_t t, uint32_t c, uint32_t st) { (void)d; (void)k; (void)s; (void)t; (void)c; (void)st; }
static void o_kmods(void *d, struct wl_keyboard *k, uint32_t s, uint32_t a, uint32_t b2, uint32_t c, uint32_t e) { (void)d; (void)k; (void)s; (void)a; (void)b2; (void)c; (void)e; }
static void o_krep(void *d, struct wl_keyboard *k, int32_t r, int32_t dl) { (void)d; (void)k; (void)r; (void)dl; }
static const struct wl_keyboard_listener o_kb_l = { o_kmap, o_kenter, o_kleave, o_kkey, o_kmods, o_krep };
static void o_offer(void *d, struct wl_data_device *dv, struct wl_data_offer *o) { (void)d; (void)dv; (void)o; }
static void o_enter(void *d, struct wl_data_device *dv, uint32_t s, struct wl_surface *f, wl_fixed_t x, wl_fixed_t y, struct wl_data_offer *o)
{ (void)d; (void)dv; (void)s; (void)f; (void)x; (void)y; (void)o; }
static void o_leave(void *d, struct wl_data_device *dv) { (void)d; (void)dv; }
static void o_motion(void *d, struct wl_data_device *dv, uint32_t t, wl_fixed_t x, wl_fixed_t y) { (void)d; (void)dv; (void)t; (void)x; (void)y; }
static void o_drop(void *d, struct wl_data_device *dv) { (void)d; (void)dv; }
static void o_sel(void *d, struct wl_data_device *dv, struct wl_data_offer *o) { (void)d; (void)dv; osel = o; }
static const struct wl_data_device_listener o_dd_l = { o_offer, o_enter, o_leave, o_motion, o_drop, o_sel };
static void o_send(void *d, struct wl_data_source *s, const char *m, int fd)
{
	(void)d; (void)s; (void)m;
	for (size_t i = 0; i < on;) {
		ssize_t w = write(fd, odata + i, on - i);
		if (w <= 0) break;
		i += (size_t)w;
	}
	close(fd);
}
static void o_target(void *d, struct wl_data_source *s, const char *m) { (void)d; (void)s; (void)m; }
static void o_cancel(void *d, struct wl_data_source *s) { (void)d; (void)s; }
static const struct wl_data_source_listener o_src_l = { .target = o_target, .send = o_send, .cancelled = o_cancel };

/* A window that takes the focus.  read (0): checks that the clipboard holds
   want.  write (1): owns the clipboard with want, closes its window so the
   editor gets the focus back, and serves pastes until ctl closes.  cover
   (2): covers the editor and goes, so the compositor draws the editor's
   whole buffer again. */
static int other(int write_it, const char *want, size_t n, int ctl)
{
	struct wl_display *d = wl_display_connect(NULL);
	if (!d) return 2;
	wl_registry_add_listener(wl_display_get_registry(d), &o_reg_l, NULL);
	wl_display_roundtrip(d);
	if (!oc || !os || !ow || !oseat || !oddm) return 3;
	struct wl_keyboard *kb = wl_seat_get_keyboard(oseat);
	wl_keyboard_add_listener(kb, &o_kb_l, NULL);
	struct wl_data_device *dd = wl_data_device_manager_get_data_device(oddm, oseat);
	wl_data_device_add_listener(dd, &o_dd_l, NULL);
	struct wl_surface *s = wl_compositor_create_surface(oc);
	struct xdg_surface *xs = xdg_wm_base_get_xdg_surface(ow, s);
	xdg_surface_add_listener(xs, &o_xs_l, NULL);
	struct xdg_toplevel *tl = xdg_surface_get_toplevel(xs);
	wl_surface_commit(s);
	wl_display_roundtrip(d);
	int w = write_it == 2 ? 645 : 100, h = write_it == 2 ? 480 : 100;   /* the cover: pixels()'s whole screen */
	int fd = memfd_create("o", 0);
	if (ftruncate(fd, w * h * 4)) return 4;
	struct wl_shm_pool *pool = wl_shm_create_pool(os, fd, w * h * 4);
	wl_surface_attach(s, wl_shm_pool_create_buffer(pool, 0, w, h, w * 4, WL_SHM_FORMAT_XRGB8888), 0, 0);
	wl_surface_commit(s);
	for (long long end = now() + 3000000000LL; !ofocus && now() < end;) wl_display_dispatch(d);
	if (!ofocus) return 5;
	if (write_it == 2) {   /* cover: shown once, then gone */
		wl_display_roundtrip(d);
		usleep(100000);
		return 0;
	}
	if (!write_it) {
		for (long long end = now() + 3000000000LL; !osel && now() < end;) wl_display_roundtrip(d);
		if (!osel) return 6;
		int p[2];
		if (pipe(p)) return 7;
		wl_data_offer_receive(osel, "text/plain;charset=utf-8", p[1]);
		close(p[1]);
		wl_display_flush(d);
		char *got = malloc(n + 1);
		size_t m = 0;
		for (ssize_t r; (r = read(p[0], got + m, n + 1 - m)) > 0;) m += (size_t)r;
		return m == n && !memcmp(got, want, n) ? 0 : 8;
	}
	odata = want;
	on = n;
	struct wl_data_source *src = wl_data_device_manager_create_data_source(oddm);
	wl_data_source_add_listener(src, &o_src_l, NULL);
	wl_data_source_offer(src, "text/plain;charset=utf-8");
	wl_data_device_set_selection(dd, src, oserial);
	xdg_toplevel_destroy(tl);
	xdg_surface_destroy(xs);
	wl_surface_destroy(s);
	wl_display_flush(d);
	if (write(ctl, "x", 1) != 1) return 9;
	struct pollfd p[2] = { { wl_display_get_fd(d), POLLIN, 0 }, { ctl, POLLIN, 0 } };
	while (poll(p, 2, 10000) > 0 && !(p[1].revents)) {
		if (wl_display_dispatch(d) < 0) break;
		wl_display_flush(d);
	}
	return 0;
}

static void clipboard(void)
{
	start("desktop", 1024, 768, 1);
	open_editor();
	int k[16], st, rows, cols;
	/* the window was given no size, so it takes the terminal's 80 x 24 */
	b->size(&rows, &cols);
	CHECK(rows == 24 && cols == 80);
	tap(30);   /* a serial to own the clipboard with */
	CHECK(pump(1000, k, 1) == 1);

	static char big[1 << 20];
	for (size_t i = 0; i < sizeof big; i++) big[i] = (char)('a' + i % 26);
	const char *small = "h\xc3\xa9llo, \xe2\x88\x80x";
	const char *tries[2] = { small, big };
	size_t lens[2] = { strlen(small), sizeof big };
	for (int t = 0; t < 2; t++) {
		/* editor to other client */
		CHECK(!b->clip_set(tries[t], lens[t]));
		static char got[1 << 20];
		CHECK(b->clip_get(got, sizeof got) == (long)lens[t] && !memcmp(got, tries[t], lens[t]));
		pid_t p = fork();
		if (!p) _exit(other(0, tries[t], lens[t], -1));
		while (waitpid(p, &st, WNOHANG) == 0) pump(10, k, 0);
		CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 0);

		/* other client to editor */
		int ctl[2];
		CHECK(!pipe(ctl));
		const char *theirs = t ? big + 1 : "from the other side";
		size_t tn = t ? sizeof big - 1 : strlen(theirs);
		if (!(p = fork())) { close(ctl[0]); _exit(other(1, theirs, tn, ctl[1])); }
		close(ctl[1]);
		char x;
		CHECK(read(ctl[0], &x, 1) == 1);
		pump(300, k, 0);   /* the focus comes back, and the offer with it */
		memset(got, 0, sizeof got);
		long n = b->clip_get(got, sizeof got);
		CHECK(n == (long)tn && !memcmp(got, theirs, tn));
		/* a buffer too small: the length, and the start */
		char few[4];
		CHECK(b->clip_get(few, sizeof few) == (long)tn && !memcmp(few, theirs, 4));
		close(ctl[0]);
		waitpid(p, &st, 0);
		CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 0);
	}
	stop();
}

/* ---- the input method weston starts: on activation it shows a preedit,
   then commits a character and sends Return ---- */

static struct zwp_input_method_context_v1 *ctx;
static uint32_t ctx_serial;

static void ic_surr(void *d, struct zwp_input_method_context_v1 *c, const char *t, uint32_t a, uint32_t e) { (void)d; (void)c; (void)t; (void)a; (void)e; }
static void ic_reset(void *d, struct zwp_input_method_context_v1 *c) { (void)d; (void)c; }
static void ic_ct(void *d, struct zwp_input_method_context_v1 *c, uint32_t h, uint32_t p) { (void)d; (void)c; (void)h; (void)p; }
static void ic_act(void *d, struct zwp_input_method_context_v1 *c, uint32_t bt, uint32_t i) { (void)d; (void)c; (void)bt; (void)i; }
static void ic_commit(void *d, struct zwp_input_method_context_v1 *c, uint32_t s) { (void)d; (void)c; ctx_serial = s; }
static void ic_lang(void *d, struct zwp_input_method_context_v1 *c, const char *l) { (void)d; (void)c; (void)l; }
static const struct zwp_input_method_context_v1_listener ic_l = { ic_surr, ic_reset, ic_ct, ic_act, ic_commit, ic_lang };
static void im_activate(void *d, struct zwp_input_method_v1 *m, struct zwp_input_method_context_v1 *c)
{
	(void)d; (void)m;
	if (ctx) return;
	ctx = c;
	zwp_input_method_context_v1_add_listener(c, &ic_l, NULL);
}
static void im_deactivate(void *d, struct zwp_input_method_v1 *m, struct zwp_input_method_context_v1 *c) { (void)d; (void)m; (void)c; }
static const struct zwp_input_method_v1_listener im_l = { im_activate, im_deactivate };
static struct zwp_input_method_v1 *im;
static void im_reg(void *d, struct wl_registry *r, uint32_t id, const char *i, uint32_t v)
{
	(void)d; (void)v;
	if (!strcmp(i, "zwp_input_method_v1")) im = wl_registry_bind(r, id, &zwp_input_method_v1_interface, 1);
}
static const struct wl_registry_listener im_reg_l = { im_reg, drv_rm };

static int input_method(void)
{
	struct wl_display *d = wl_display_connect(NULL);
	if (!d) return 1;
	wl_registry_add_listener(wl_display_get_registry(d), &im_reg_l, NULL);
	wl_display_roundtrip(d);
	if (!im) return 1;
	zwp_input_method_v1_add_listener(im, &im_l, NULL);
	while (!ctx) if (wl_display_dispatch(d) < 0) return 1;
	usleep(1000000);   /* after the editor's first frame */
	wl_display_roundtrip(d);
	zwp_input_method_context_v1_preedit_string(ctx, ctx_serial, "ni", "ni");
	wl_display_flush(d);
	usleep(1000000);
	wl_display_roundtrip(d);
	zwp_input_method_context_v1_commit_string(ctx, ctx_serial, "\xe4\xbd\xa0");
	zwp_input_method_context_v1_keysym(ctx, ctx_serial, 0, 0xff0d, WL_KEYBOARD_KEY_STATE_PRESSED, 0);
	zwp_input_method_context_v1_keysym(ctx, ctx_serial, 0, 0xff0d, WL_KEYBOARD_KEY_STATE_RELEASED, 0);
	wl_display_flush(d);
	while (wl_display_dispatch(d) >= 0) {}
	return 0;
}

static void ime(void)
{
	start("desktop", 1024, 768, 1);
	open_editor();
	int k[8], n = 0;
	/* the preedit is on screen before anything is committed, though the
	   core drew nothing: the backend drew a frame of its own */
	int cw, ch, base;
	font_cell(&cw, &ch, &base);
	/* pixels of the preedit's background: anti-aliased text has some too,
	   so count the new ones (the window's place on the output is the
	   shell's) */
	long pre = 0, before = -1;
	for (long long end = now() + 5000000000LL; now() < end && pre - before < cw * ch / 2 && !n;) {
		n += pump(20, k + n, 2 - n);
		uint32_t *px = capture(0);
		pre = 0;
		for (int i = 0; i < cap_w * cap_h; i++) pre += px[i] == 0x45475a;
		if (before < 0) before = pre;
		free(px);
	}
	CHECK(pre - before >= cw * ch / 2 && n == 0);
	n += pump(3000, k + n, 2 - n);
	CHECK(n == 2 && k[0] == 0x4f60 && k[1] == KEY_ENTER);
	stop();
}

/* ---- measurements (WL_MEASURE=1, not part of the check) ---- */

static int cmp_ll(const void *x, const void *y)
{
	long long a = *(const long long *)x, c = *(const long long *)y;
	return (a > c) - (a < c);
}

static void stats(const char *what, long long *v, int n)
{
	qsort(v, (size_t)n, sizeof *v, cmp_ll);
	printf("%s: n %d, p50 %.3f ms, p99 %.3f ms, max %.3f ms\n", what, n, v[n / 2] / 1e6, v[n * 99 / 100] / 1e6, v[n - 1] / 1e6);
}

/* The review's run: N keys typed at a fixed rate (driver -> weston ->
   backend), each appended to row 0 as the core would; a frame is drawn
   whenever the backend is ready.  Reports frames per key, key to commit,
   and the time to draw a frame (frame ready). */
static void measure(void)
{
	const char *e = getenv("WL_MEASURE_SIZE");
	int w = 1600, h = 1000, keys_n = 2000, gap_us = 1000;
	if (e) sscanf(e, "%dx%d", &w, &h);
	start("kiosk", w, h, 1);
	open_editor();
	static long long sent[4096], lat[4096], draw[4096];
	int fds[2], nf = b->fds(fds), got = 0, frames = 0, ns = 0, shown_upto = 0;
	struct pollfd p[2] = { { fds[0], POLLIN, 0 }, { fds[1], POLLIN, 0 } };
	long long next = now();
	while (got < keys_n || shown_upto < keys_n) {
		if (ns < keys_n && now() >= next) {
			sent[ns++] = now();
			tap(45);   /* x */
			next += gap_us * 1000LL;
		}
		if (shown_upto < got && b->ready()) {
			for (int i = shown_upto; i < got; i++) grid_put(&g, 0, i % g.cols, (const unsigned char *)"x", 1, 1, 0);
			g.ccol = got % g.cols;
			long long t = now();
			b->present(&g, &shown);
			long long t2 = now();
			draw[frames++] = t2 - t;
			for (int i = shown_upto; i < got; i++) lat[i] = t2 - sent[i];
			shown_upto = got;
		}
		int to = ns < keys_n ? (int)((next - now()) / 1000000) : 20;
		if (poll(p, (nfds_t)nf, to < 0 ? 0 : to) <= 0) continue;
		int k[256], m = b->input(k, 256);
		for (int i = 0; i < m; i++) if (k[i] == 'x') got++;
	}
	printf("%dx%d, %d keys every %d us: %d frames drawn\n", w, h, keys_n, gap_us, frames);
	stats("key to commit", lat, keys_n);
	stats("frame ready (draw one changed row)", draw, frames);
	/* the whole window, as the prototype measured it */
	for (int i = 0; i < 50; i++) {
		settle();
		for (int r = 0; r < g.rows; r++) grid_puts(&g, r, 0, i & 1 ? "int main(void) { return 0; } // ->" : "fn f() -> u8 { 1 } /* != */", r % 3);
		grid_free(&shown);
		long long t = now();
		b->present(&g, &shown);
		draw[i] = now() - t;
	}
	stats("frame ready (whole window)", draw, 50);
	stop();
}

/* WL_ONLY=text runs the scenarios whose names contain it */
static void run(const char *name, void (*f)(void))
{
	if (getenv("WL_ONLY") && !strstr(name, getenv("WL_ONLY"))) return;
	pid_t p = fork();
	if (!p) { f(); _exit(0); }
	int st;
	waitpid(p, &st, 0);
	if (!WIFEXITED(st) || WEXITSTATUS(st)) { printf("FAIL %s\n", name); exit(1); }
	printf("ok %s\n", name);
}

static void scale1(void) { sized(1); }
static void scale2(void) { sized(2); }

int main(void)
{
	if (getenv("WL_TEST_IM")) return input_method();
	ssize_t n = readlink("/proc/self/exe", self, sizeof self - 1);
	CHECK(n > 0);
	setvbuf(stdout, NULL, _IONBF, 0);
	if (getenv("WL_MEASURE")) { run("measure", measure); return 0; }
	run("configured size, scale 1", scale1);
	run("configured size, scale 2", scale2);
	run("frame callbacks", frames);
	run("pieces equal a full redraw", pixels);
	run("keys, repeat, idle, pointer", keys);
	run("clipboard, unconfigured size", clipboard);
	run("IME", ime);
	run("the editor in a window edits and saves", editor);
	run("the compositor gone", gone);
	return 0;
}
