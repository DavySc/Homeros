/* Wayland backend: an xdg-shell window drawn on the CPU into wl_shm buffers,
   as foot does.  Frames follow Present.tla's "framecb" policy: draw only
   after the compositor's frame callback and into a buffer it has released.
   Keys come from xkbcommon with client-side repeat on a timerfd, text from
   an input method through text-input-v3, the "+ register through
   wl_data_device; the pointer gets the text cursor (cursor-shape-v1) and
   gives left clicks and the wheel as keys; the buffer is drawn at the fractional scale the compositor prefers
   (fractional-scale-v1 with a viewport).  These are the protocols Hyprland
   offers; the window has no decorations (Hyprland draws none for it). */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/timerfd.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>
#include "xdg-shell-client-protocol.h"
#include "text-input-unstable-v3-client-protocol.h"
#include "cursor-shape-v1-client-protocol.h"
#include "fractional-scale-v1-client-protocol.h"
#include "viewporter-client-protocol.h"
#include "backend.h"
#include "loop.h"
#include "keys.h"
#include "uni.h"
#include "font.h"
#include "paint.h"

/* FONT_PX: pixels per em at scale 1 (hypothesis: Davy's size is not recorded) */
enum { FONT_PX = 16, NBUF = 2, KQ = 1024, PRE = 256 };
static const char FAMILY[] = "JetBrains Mono";
/* linux/input-event-codes.h's, whose KEY_* names clash with keys.h's */
enum { BTN_LEFT = 0x110 };
static const char MIME[] = "text/plain;charset=utf-8";

static struct wl_display *dpy;
static struct wl_compositor *comp;
static struct wl_shm *shm;
static struct xdg_wm_base *wm;
static struct wl_seat *seat;
static struct wl_data_device_manager *ddm;
static struct zwp_text_input_manager_v3 *tim;
static struct wp_cursor_shape_manager_v1 *csm;
static struct wp_fractional_scale_manager_v1 *fsm;
static struct wp_viewporter *vpr;

static struct wl_surface *surf;
static struct xdg_surface *xsurf;
static struct xdg_toplevel *top;
static struct wp_viewport *view;
static struct wl_keyboard *kbd;
static struct wl_pointer *ptr;
static struct wp_cursor_shape_device_v1 *shape;
static struct zwp_text_input_v3 *ti;
static struct wl_data_device *dd;

static int cell_w, cell_h, cell_b;  /* the font's cell and baseline, in pixels */
static int lw = 800, lh = 600;
static int font(int px)
{
	if (font_open(FAMILY, px)) return -1;
	font_cell(&cell_w, &cell_h, &cell_b);
	return 0;
}      /* logical size, from configure */
static int scale = 120;             /* in 120ths, fractional-scale-v1's unit */
static int bw, bh;                  /* buffer size in pixels */
static int configured, waiting;     /* waiting: a frame callback is due */
static struct wl_callback *frame;
static struct wp_fractional_scale_v1 *fscale;
static struct wl_registry *reg;

static struct buf {
	struct wl_buffer *b;
	uint32_t *px;
	int busy;
	Grid g;                         /* what this buffer shows */
} bufs[NBUF], *on;                  /* on: the buffer last committed, on screen */
static void *pool_mem;
static int pw, ph;                  /* size of the buffers in the pool */
static Grid vis;                    /* cur with the preedit, when there is one */
static size_t pool_size;

static int kq[KQ], kh, kn;          /* keys for wl_input */
static void push(int k) { if (kn < KQ) kq[(kh + kn++) % KQ] = k; }
static void push_utf8(const char *s)
{
	size_t n = strlen(s);
	for (size_t i = 0; i < n; i += uni_len((const unsigned char *)s, n, i))
		push((int)uni_cp((const unsigned char *)s, n, i));
}

/* ---- keyboard ---- */

static struct xkb_context *xctx;
static struct xkb_keymap *keymap;
static struct xkb_state *xst;
static int rfd = -1, rate, delay;   /* key repeat: timerfd, keys/s, ms */
static uint32_t rkey;               /* the key repeating, 0 none */
static long long rnext;             /* when it repeats next, CLOCK_MONOTONIC ns */
static uint32_t serial;             /* last input serial, for the clipboard */

static void repeat_arm(uint32_t key)
{
	rkey = rate > 0 ? key : 0;
	rnext = loop_now() + delay * 1000000LL;
}

/* The timer fires at the next repeat, or at once while keys wait in kq
   (an absolute 1 ns is long past): main.c calls wl_input only when an fd
   is readable, and the socket may not be (as build B does) */
static void arm(void)
{
	long long at = kn ? 1 : rkey ? rnext : 0;
	struct itimerspec t = { .it_value = { (time_t)(at / 1000000000), (long)(at % 1000000000) } };
	timerfd_settime(rfd, TFD_TIMER_ABSTIME, &t, NULL);
}

static int key_of(uint32_t key)
{
	xkb_keysym_t s = xkb_state_key_get_one_sym(xst, key + 8);
	switch (s) {
	case XKB_KEY_Up: return KEY_UP;
	case XKB_KEY_Down: return KEY_DOWN;
	case XKB_KEY_Left: return KEY_LEFT;
	case XKB_KEY_Right: return KEY_RIGHT;
	case XKB_KEY_BackSpace: return KEY_BS;     /* what a terminal sends */
	case XKB_KEY_ISO_Left_Tab: return KEY_S_TAB;   /* Shift-Tab in most layouts */
	case XKB_KEY_Tab:   /* and in a keymap whose Tab has one level (wtype's) */
		if (xkb_state_mod_name_is_active(xst, XKB_MOD_NAME_SHIFT, XKB_STATE_MODS_EFFECTIVE) > 0) return KEY_S_TAB;
		break;
	/* Control leaves '.' as it is: no control character has a dot */
	case XKB_KEY_period:
		if (xkb_state_mod_name_is_active(xst, XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE) > 0) return KEY_CTRL_DOT;
	}
	/* includes the Control transformation, so Ctrl-R is 18 */
	return (int)xkb_state_key_get_utf32(xst, key + 8);
}

static void kb_keymap(void *d, struct wl_keyboard *k, uint32_t fmt, int fd, uint32_t size)
{
	(void)d; (void)k;
	char *m = fmt == WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1 ? mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0) : MAP_FAILED;
	close(fd);
	if (m == MAP_FAILED) return;
	struct xkb_keymap *km = xkb_keymap_new_from_string(xctx, m, XKB_KEYMAP_FORMAT_TEXT_V1, 0);
	munmap(m, size);
	if (!km) return;
	xkb_state_unref(xst);
	xkb_keymap_unref(keymap);
	keymap = km;
	xst = xkb_state_new(km);
}

static void kb_enter(void *d, struct wl_keyboard *k, uint32_t s, struct wl_surface *f, struct wl_array *a)
{
	(void)d; (void)k; (void)f; (void)a;
	serial = s;
}

static void kb_leave(void *d, struct wl_keyboard *k, uint32_t s, struct wl_surface *f)
{
	(void)d; (void)k; (void)s; (void)f;
	repeat_arm(0);
}

static void kb_key(void *d, struct wl_keyboard *k, uint32_t s, uint32_t t, uint32_t key, uint32_t st)
{
	(void)d; (void)k; (void)t;
	serial = s;
	if (!xst) return;
	if (st != WL_KEYBOARD_KEY_STATE_PRESSED) {
		if (key == rkey) repeat_arm(0);
		return;
	}
	int c = key_of(key);
	if (c) push(c);
	if (c && xkb_keymap_key_repeats(keymap, key + 8)) repeat_arm(key);
	else if (rkey) repeat_arm(0);
}

static void kb_mods(void *d, struct wl_keyboard *k, uint32_t s, uint32_t dep, uint32_t lat, uint32_t lock, uint32_t grp)
{
	(void)d; (void)k; (void)s;
	if (xst) xkb_state_update_mask(xst, dep, lat, lock, 0, 0, grp);
}

static void kb_repeat(void *d, struct wl_keyboard *k, int32_t r, int32_t dl)
{
	(void)d; (void)k;
	rate = r;
	delay = dl;
}

static const struct wl_keyboard_listener kb_l = { kb_keymap, kb_enter, kb_leave, kb_key, kb_mods, kb_repeat };

/* ---- pointer: the text cursor; a left click and the wheel are keys at
   the pointer's cell (Neovim's <LeftMouse>, <ScrollWheelUp/Down>) ---- */

static int mx, my, mrow, mcol;      /* pointer in buffer pixels; the cell of the last mouse key */
static int finger, wheel;           /* this frame's scroll has no notches (touchpad); 256ths of a pixel or 120ths of a notch scrolled */

static void mouse_key(int k)
{
	mrow = my / cell_h;
	mcol = mx / cell_w;
	push(k);
}
static void pt_enter(void *d, struct wl_pointer *p, uint32_t s, struct wl_surface *f, wl_fixed_t x, wl_fixed_t y)
{
	(void)d; (void)p; (void)f;
	if (shape) wp_cursor_shape_device_v1_set_shape(shape, s, WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_TEXT);
	mx = wl_fixed_to_int(x) * scale / 120;
	my = wl_fixed_to_int(y) * scale / 120;
}
static void pt_leave(void *d, struct wl_pointer *p, uint32_t s, struct wl_surface *f) { (void)d; (void)p; (void)s; (void)f; }
static void pt_motion(void *d, struct wl_pointer *p, uint32_t t, wl_fixed_t x, wl_fixed_t y)
{
	(void)d; (void)p; (void)t;
	mx = wl_fixed_to_int(x) * scale / 120;
	my = wl_fixed_to_int(y) * scale / 120;
}
static void pt_button(void *d, struct wl_pointer *p, uint32_t s, uint32_t t, uint32_t b, uint32_t st)
{
	(void)d; (void)p; (void)s; (void)t;
	if (b == BTN_LEFT && st == WL_POINTER_BUTTON_STATE_PRESSED) mouse_key(KEY_MOUSE_LEFT);
}
/* A wheel key per notch (120); a touchpad's pixels give one per 3 rows,
   what a wheel key scrolls in Neovim, so the text follows the fingers. */
static void scroll(int v, int step)
{
	for (wheel += v; wheel >= step; wheel -= step) mouse_key(KEY_WHEEL_DOWN);
	for (; wheel <= -step; wheel += step) mouse_key(KEY_WHEEL_UP);
}
static void pt_axis(void *d, struct wl_pointer *p, uint32_t t, uint32_t a, wl_fixed_t v)
{
	(void)d; (void)p; (void)t;
	if (finger && a == WL_POINTER_AXIS_VERTICAL_SCROLL) scroll(v, 3 * cell_h * 256 * 120 / scale);   /* in wl_fixed's 1/256 */
}
static void pt_frame(void *d, struct wl_pointer *p) { (void)d; (void)p; finger = 0; }
static void pt_source(void *d, struct wl_pointer *p, uint32_t s)
{
	(void)d; (void)p;
	finger = s != WL_POINTER_AXIS_SOURCE_WHEEL;
}
static void pt_stop(void *d, struct wl_pointer *p, uint32_t t, uint32_t a) { (void)d; (void)p; (void)t; (void)a; wheel = 0; }
static void pt_value120(void *d, struct wl_pointer *p, uint32_t a, int32_t v)
{
	(void)d; (void)p;
	if (a == WL_POINTER_AXIS_VERTICAL_SCROLL) scroll(v, 120);
}
/* the seat is bound at version 8 (value120), so these are all the events
   that come (axis_discrete is not sent from 8 on); listeners name their
   fields, since newer headers add events at the end */
static const struct wl_pointer_listener pt_l = { .enter = pt_enter, .leave = pt_leave, .motion = pt_motion,
	.button = pt_button, .axis = pt_axis, .frame = pt_frame, .axis_source = pt_source, .axis_stop = pt_stop,
	.axis_value120 = pt_value120 };

/* ---- input method (text-input-v3): committed text becomes keys; the
   preedit is drawn at the cursor until the next done ---- */

static char pre[PRE], pre_next[PRE], commit_next[PRE * 4];
static int ti_on, ti_r, ti_c;       /* enabled; the cursor cell it was told */

static void ti_cursor(void)
{
	if (!ti_on) return;
	zwp_text_input_v3_set_cursor_rectangle(ti, ti_c * cell_w * 120 / scale, ti_r * cell_h * 120 / scale,
		cell_w * 120 / scale, cell_h * 120 / scale);
	zwp_text_input_v3_commit(ti);
}
static void ti_enter(void *d, struct zwp_text_input_v3 *t, struct wl_surface *f)
{
	(void)d; (void)f;
	zwp_text_input_v3_enable(t);
	ti_on = 1;
	ti_cursor();
}
static void ti_leave(void *d, struct zwp_text_input_v3 *t, struct wl_surface *f)
{
	(void)d; (void)f;
	zwp_text_input_v3_disable(t);
	zwp_text_input_v3_commit(t);
	ti_on = 0;
	if (pre[0]) { pre[0] = 0; push(KEY_RESIZE); }
}
static void ti_preedit(void *d, struct zwp_text_input_v3 *t, const char *s, int32_t b, int32_t e)
{
	(void)d; (void)t; (void)b; (void)e;
	snprintf(pre_next, sizeof pre_next, "%s", s ? s : "");
}
static void ti_commit(void *d, struct zwp_text_input_v3 *t, const char *s)
{
	(void)d; (void)t;
	snprintf(commit_next, sizeof commit_next, "%s", s ? s : "");
}
static void ti_delete(void *d, struct zwp_text_input_v3 *t, uint32_t b, uint32_t a) { (void)d; (void)t; (void)b; (void)a; }
static void ti_done(void *d, struct zwp_text_input_v3 *t, uint32_t s)
{
	(void)d; (void)t; (void)s;
	push_utf8(commit_next);
	commit_next[0] = 0;
	if (strcmp(pre, pre_next)) { memcpy(pre, pre_next, sizeof pre); push(KEY_RESIZE); }
	pre_next[0] = 0;
}
static const struct zwp_text_input_v3_listener ti_l = { .enter = ti_enter, .leave = ti_leave, .preedit_string = ti_preedit,
	.commit_string = ti_commit, .delete_surrounding_text = ti_delete, .done = ti_done };

/* ---- seat ---- */

/* A keyboard or pointer that goes away and comes back needs a new object:
   the old one stays inert (wlroots), so it is released, as foot does. */
static void seat_caps(void *d, struct wl_seat *s, uint32_t caps)
{
	(void)d;
	if (caps & WL_SEAT_CAPABILITY_KEYBOARD && !kbd) {
		kbd = wl_seat_get_keyboard(s);
		wl_keyboard_add_listener(kbd, &kb_l, NULL);
	} else if (!(caps & WL_SEAT_CAPABILITY_KEYBOARD) && kbd) {
		wl_keyboard_release(kbd);
		kbd = NULL;
		repeat_arm(0);
	}
	if (caps & WL_SEAT_CAPABILITY_POINTER && !ptr) {
		ptr = wl_seat_get_pointer(s);
		wl_pointer_add_listener(ptr, &pt_l, NULL);
		if (csm) shape = wp_cursor_shape_manager_v1_get_pointer(csm, ptr);
	} else if (!(caps & WL_SEAT_CAPABILITY_POINTER) && ptr) {
		if (shape) wp_cursor_shape_device_v1_destroy(shape);
		wl_pointer_release(ptr);
		ptr = NULL;
		shape = NULL;
	}
}
static void seat_name(void *d, struct wl_seat *s, const char *n) { (void)d; (void)s; (void)n; }
static const struct wl_seat_listener seat_l = { seat_caps, seat_name };


/* ---- clipboard (the "+ register) ---- */

static struct wl_data_offer *sel, *dnd;   /* the selection offered to us, a drag over us */
static struct wl_data_source *src;        /* our selection, while we own it */
static char *clip, *got;            /* our selection's text; the last one read */
static size_t clipn, gotn;

/* Reads up to n bytes from a pipe; stops at end of file, or after a
   second without progress, as Neovim's clipboard provider times out. */
static size_t xfer(int fd, char *b, size_t n)
{
	size_t i = 0;
	struct pollfd p = { fd, POLLIN, 0 };
	while (i < n && poll(&p, 1, 1000) > 0) {
		ssize_t r = read(fd, b + i, n - i);
		if (r < 0 && errno == EINTR) continue;
		if (r <= 0) break;
		i += (size_t)r;
	}
	return i;
}

static void src_target(void *d, struct wl_data_source *s, const char *m) { (void)d; (void)s; (void)m; }
static void send_all(int fd)
{
	for (size_t i = 0; i < clipn;) {
		ssize_t r = write(fd, clip + i, clipn - i);
		if (r < 0 && errno == EINTR) continue;
		if (r <= 0) break;
		i += (size_t)r;
	}
}
static void src_send(void *d, struct wl_data_source *s, const char *m, int fd)
{
	(void)d; (void)s; (void)m;
	/* written by a grandchild, as wl-copy forks one for each paste, so a
	   reader that stalls never stops the editor (the review of package 8,
	   finding 3); the child exits at once and is reaped here */
	pid_t p = fork();
	if (!p) {
		if (!fork()) send_all(fd);
		_exit(0);
	}
	if (p > 0) waitpid(p, NULL, 0);
	close(fd);
}
static void src_cancelled(void *d, struct wl_data_source *s)
{
	(void)d;
	wl_data_source_destroy(s);
	if (s == src) src = NULL;
}
static const struct wl_data_source_listener src_l = { .target = src_target, .send = src_send, .cancelled = src_cancelled };

/* An offer's user data is non-null once it offers MIME. */
static void offer_mime(void *d, struct wl_data_offer *o, const char *m)
{
	(void)d;
	if (!strcmp(m, MIME)) wl_data_offer_set_user_data(o, o);
}
static const struct wl_data_offer_listener offer_l = { .offer = offer_mime };

static void dd_offer(void *d, struct wl_data_device *v, struct wl_data_offer *o)
{
	(void)d; (void)v;
	wl_data_offer_add_listener(o, &offer_l, NULL);
}
static void dd_enter(void *d, struct wl_data_device *v, uint32_t s, struct wl_surface *f, wl_fixed_t x, wl_fixed_t y, struct wl_data_offer *o)
{
	(void)d; (void)v; (void)s; (void)f; (void)x; (void)y;
	dnd = o;
}
static void dd_leave(void *d, struct wl_data_device *v)
{
	(void)d; (void)v;
	if (dnd) wl_data_offer_destroy(dnd);
	dnd = NULL;
}
static void dd_motion(void *d, struct wl_data_device *v, uint32_t t, wl_fixed_t x, wl_fixed_t y) { (void)d; (void)v; (void)t; (void)x; (void)y; }
static void dd_drop(void *d, struct wl_data_device *v) { dd_leave(d, v); }
static void dd_selection(void *d, struct wl_data_device *v, struct wl_data_offer *o)
{
	(void)d; (void)v;
	if (sel) wl_data_offer_destroy(sel);
	sel = o;
	free(got);
	got = NULL;
	gotn = 0;
}
static const struct wl_data_device_listener dd_l = { dd_offer, dd_enter, dd_leave, dd_motion, dd_drop, dd_selection };

static int wl_clip_set(const char *s, size_t n)
{
	char *c = malloc(n ? n : 1);
	if (!c || !dd) { free(c); return -1; }
	memcpy(c, s, n);
	free(clip);
	clip = c;
	clipn = n;
	if (!src) {
		src = wl_data_device_manager_create_data_source(ddm);
		wl_data_source_add_listener(src, &src_l, NULL);
		wl_data_source_offer(src, MIME);
		wl_data_source_offer(src, "text/plain");
		wl_data_device_set_selection(dd, src, serial);
	}
	wl_display_flush(dpy);
	return 0;
}

/* The selection is read once and kept until it changes, for a second call
   with more room. */
static long wl_clip_get(char *buf, size_t max)
{
	const char *s = clip;      /* ours: the compositor would ask us to send it to ourselves */
	size_t n = clipn;
	int p[2];
	if (!src) {
		if (!got && sel && wl_data_offer_get_user_data(sel) && !pipe2(p, O_CLOEXEC)) {
			wl_data_offer_receive(sel, MIME, p[1]);
			close(p[1]);
			wl_display_flush(dpy);
			for (size_t cap = 4096;; cap *= 2) {
				char *c = realloc(got, cap);
				if (!c) { free(got); got = NULL; break; }
				got = c;
				gotn += xfer(p[0], got + gotn, cap - gotn);
				if (gotn < cap) break;
			}
			close(p[0]);
		}
		if (!got) return -1;
		s = got;
		n = gotn;
	}
	memcpy(buf, s, n < max ? n : max);
	return (long)n;
}

/* ---- window ---- */

static void wm_ping(void *d, struct xdg_wm_base *w, uint32_t s) { (void)d; xdg_wm_base_pong(w, s); }
static const struct xdg_wm_base_listener wm_l = { wm_ping };

static int nlw, nlh;                /* the size of the configure in progress */
static void top_configure(void *d, struct xdg_toplevel *t, int32_t w, int32_t h, struct wl_array *st)
{
	(void)d; (void)t; (void)st;
	if (w > 0) nlw = w;            /* 0: the client chooses, so keep ours */
	if (h > 0) nlh = h;
}
static void top_close(void *d, struct xdg_toplevel *t) { (void)d; (void)t; push(KEY_CLOSE); }
static const struct xdg_toplevel_listener top_l = { .configure = top_configure, .close = top_close };

static void resize(void)
{
	bw = (lw * scale + 60) / 120;  /* fractional-scale-v1: round half away from zero */
	bh = (lh * scale + 60) / 120;
	if (view) wp_viewport_set_destination(view, lw, lh);
	push(KEY_RESIZE);
}

static void xs_configure(void *d, struct xdg_surface *x, uint32_t s)
{
	(void)d;
	xdg_surface_ack_configure(x, s);
	if (!configured || nlw != lw || nlh != lh) {
		lw = nlw;
		lh = nlh;
		resize();
	}
	configured = 1;
}
static const struct xdg_surface_listener xs_l = { xs_configure };

static void fs_scale(void *d, struct wp_fractional_scale_v1 *f, uint32_t s)
{
	(void)d; (void)f;
	if ((int)s == scale || font((FONT_PX * (int)s + 60) / 120)) return;
	scale = (int)s;
	resize();
}
static const struct wp_fractional_scale_v1_listener fs_l = { fs_scale };

static void reg_add(void *d, struct wl_registry *r, uint32_t id, const char *i, uint32_t v)
{
	(void)d;
#define BIND(var, iface, ver) if (!strcmp(i, iface.name)) var = wl_registry_bind(r, id, &iface, v < ver ? v : ver)
	BIND(comp, wl_compositor_interface, 4);
	else BIND(shm, wl_shm_interface, 1);
	else BIND(wm, xdg_wm_base_interface, 1);
	else if (!strcmp(i, wl_seat_interface.name) && !seat) {
		/* its capabilities come right after the bind, in the same roundtrip */
		BIND(seat, wl_seat_interface, 8);
		wl_seat_add_listener(seat, &seat_l, NULL);
	}
	else BIND(ddm, wl_data_device_manager_interface, 1);   /* selection only: no drag-and-drop events */
	else BIND(tim, zwp_text_input_manager_v3_interface, 1);
	else BIND(csm, wp_cursor_shape_manager_v1_interface, 1);
	else BIND(fsm, wp_fractional_scale_manager_v1_interface, 1);
	else BIND(vpr, wp_viewporter_interface, 1);
#undef BIND
}
static void reg_remove(void *d, struct wl_registry *r, uint32_t id) { (void)d; (void)r; (void)id; }
static const struct wl_registry_listener reg_l = { reg_add, reg_remove };

/* ---- drawing ---- */

static void buf_release(void *d, struct wl_buffer *b) { (void)b; ((struct buf *)d)->busy = 0; }
static const struct wl_buffer_listener buf_l = { buf_release };

static void bufs_free(void)
{
	for (int i = 0; i < NBUF; i++) {
		if (bufs[i].b) wl_buffer_destroy(bufs[i].b);
		bufs[i].b = NULL;
		bufs[i].busy = 0;
		grid_free(&bufs[i].g);
	}
	if (pool_mem) munmap(pool_mem, pool_size);
	pool_mem = NULL;
	pw = ph = 0;
}

/* Buffers of the current size; a busy one is destroyed too, which the
   protocol allows (the compositor keeps the contents it shows). */
static int bufs_make(void)
{
	bufs_free();
	size_t one = (size_t)bw * (size_t)bh * 4;
	int fd = memfd_create("homeros", MFD_CLOEXEC);
	if (fd < 0) return -1;
	pool_size = one * NBUF;
	pool_mem = ftruncate(fd, (off_t)pool_size) ? MAP_FAILED : mmap(NULL, pool_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (pool_mem == MAP_FAILED) { close(fd); pool_mem = NULL; return -1; }
	struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, (int32_t)pool_size);
	for (int i = 0; i < NBUF; i++) {
		bufs[i].px = (uint32_t *)((char *)pool_mem + one * (size_t)i);
		bufs[i].b = wl_shm_pool_create_buffer(pool, (int32_t)(one * (size_t)i), bw, bh, bw * 4, WL_SHM_FORMAT_XRGB8888);
		wl_buffer_add_listener(bufs[i].b, &buf_l, &bufs[i]);
	}
	wl_shm_pool_destroy(pool);
	close(fd);
	return 0;
}

/* ---- backend.h ---- */

static void frame_done(void *d, struct wl_callback *cb, uint32_t t)
{
	(void)d; (void)t;
	wl_callback_destroy(cb);
	frame = NULL;
	waiting = 0;
}
static const struct wl_callback_listener frame_l = { frame_done };

static void wl_close(void)
{
	bufs_free();
	if (frame) wl_callback_destroy(frame);
	if (sel) wl_data_offer_destroy(sel);
	if (dnd) wl_data_offer_destroy(dnd);
	if (src) wl_data_source_destroy(src);
	if (dd) wl_data_device_destroy(dd);
	if (ti) zwp_text_input_v3_destroy(ti);
	if (shape) wp_cursor_shape_device_v1_destroy(shape);
	if (ptr) wl_pointer_release(ptr);
	if (kbd) wl_keyboard_release(kbd);
	if (top) xdg_toplevel_destroy(top);
	if (xsurf) xdg_surface_destroy(xsurf);
	if (view) wp_viewport_destroy(view);
	if (fscale) wp_fractional_scale_v1_destroy(fscale);
	if (surf) wl_surface_destroy(surf);
	if (vpr) wp_viewporter_destroy(vpr);
	if (fsm) wp_fractional_scale_manager_v1_destroy(fsm);
	if (csm) wp_cursor_shape_manager_v1_destroy(csm);
	if (tim) zwp_text_input_manager_v3_destroy(tim);
	if (ddm) wl_data_device_manager_destroy(ddm);
	if (seat) wl_seat_destroy(seat);
	if (wm) xdg_wm_base_destroy(wm);
	if (shm) wl_shm_destroy(shm);
	if (comp) wl_compositor_destroy(comp);
	if (reg) wl_registry_destroy(reg);
	if (dpy) wl_display_disconnect(dpy);
	xkb_state_unref(xst);
	xkb_keymap_unref(keymap);
	xkb_context_unref(xctx);
	if (rfd >= 0) close(rfd);
	font_close();
	free(clip);
	free(got);
	grid_free(&vis);
	frame = NULL; sel = dnd = NULL; src = NULL; dd = NULL; ti = NULL; shape = NULL; ptr = NULL; kbd = NULL;
	top = NULL; xsurf = NULL; view = NULL; fscale = NULL; surf = NULL; vpr = NULL; fsm = NULL; csm = NULL;
	tim = NULL; ddm = NULL; seat = NULL; wm = NULL; shm = NULL; comp = NULL; reg = NULL; dpy = NULL;
	xst = NULL; keymap = NULL; xctx = NULL; rfd = -1; clip = got = NULL;
	gotn = 0;
	configured = waiting = ti_on = kn = 0;
	rkey = 0;
	scale = 120;
	lw = 800;
	lh = 600;
}

static int wl_open(void)
{
	if (!(dpy = wl_display_connect(NULL))) return -1;
	reg = wl_display_get_registry(dpy);
	wl_registry_add_listener(reg, &reg_l, NULL);
	wl_display_flush(dpy);        /* the compositor answers while the font loads */
	if (font(FONT_PX) || !(xctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS))
	    || (rfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC)) < 0
	    || wl_display_roundtrip(dpy) < 0 || !comp || !shm || !wm) {
		wl_close();
		return -1;
	}
	xdg_wm_base_add_listener(wm, &wm_l, NULL);
	if (seat) {
		if (ddm) wl_data_device_add_listener(dd = wl_data_device_manager_get_data_device(ddm, seat), &dd_l, NULL);
		if (tim) zwp_text_input_v3_add_listener(ti = zwp_text_input_manager_v3_get_text_input(tim, seat), &ti_l, NULL);
	}
	surf = wl_compositor_create_surface(comp);
	if (fsm && vpr) {
		wp_fractional_scale_v1_add_listener(fscale = wp_fractional_scale_manager_v1_get_fractional_scale(fsm, surf), &fs_l, NULL);
		view = wp_viewporter_get_viewport(vpr, surf);
	}
	xdg_surface_add_listener(xsurf = xdg_wm_base_get_xdg_surface(wm, surf), &xs_l, NULL);
	xdg_toplevel_add_listener(top = xdg_surface_get_toplevel(xsurf), &top_l, NULL);
	xdg_toplevel_set_title(top, "homeros");
	xdg_toplevel_set_app_id(top, "homeros");
	wl_surface_commit(surf);
	nlw = lw;
	nlh = lh;
	while (!configured)
		if (wl_display_dispatch(dpy) < 0) { wl_close(); return -1; }
	kn = 0;                       /* the first size is read with wl_size */
	return 0;
}

static int wl_fds(int fds[2])
{
	fds[0] = wl_display_get_fd(dpy);
	fds[1] = rfd;
	return 2;
}

static int wl_input(int *out, int n)
{
	uint64_t x;
	if (read(rfd, &x, sizeof x) < 0) {}   /* only clears the timer */
	int e = 0;
	while (!e && wl_display_prepare_read(dpy)) e = wl_display_dispatch_pending(dpy) < 0;
	if (!e) e = wl_display_read_events(dpy) < 0;   /* reads what is there, never blocks */
	if (e || wl_display_dispatch_pending(dpy) < 0) return -1;   /* the compositor is gone */
	wl_display_flush(dpy);
	long long t = loop_now();
	if (rkey && rate > 0 && t >= rnext) {
		long long per = 1000000000LL / rate, c = 1 + (t - rnext) / per;
		for (long long j = 0, k = key_of(rkey); j < c && kn < KQ; j++) push((int)k);
		rnext += c * per;
	}
	int i = 0;
	for (; i < n && kn; i++, kn--, kh = (kh + 1) % KQ) out[i] = kq[kh];
	arm();
	return i;
}

static void wl_size(int *rows, int *cols)
{
	*rows = bh / cell_h > 0 ? bh / cell_h : 1;
	*cols = bw / cell_w > 0 ? bw / cell_w : 1;
}

static int wl_ready(void)
{
	if (!configured || waiting) return 0;
	if (pw != bw || ph != bh) return 1;
	for (int i = 0; i < NBUF; i++) if (!bufs[i].busy) return 1;
	return 0;
}

/* cur with the preedit put at the cursor */
static const Grid *overlay(const Grid *cur)
{
	if (!pre[0]) return cur;
	if ((vis.rows != cur->rows || vis.cols != cur->cols) && grid_resize(&vis, cur->rows, cur->cols)) return cur;
	memcpy(vis.c, cur->c, sizeof *cur->c * (size_t)cur->rows * (size_t)cur->cols);
	vis.crow = cur->crow;
	vis.ccol = cur->ccol;
	size_t n = strlen(pre);
	const unsigned char *s = (const unsigned char *)pre;
	for (size_t i = 0, c = (size_t)cur->ccol; i < n; i += uni_len(s, n, i))   /* an input method sends valid UTF-8 */
		c += (size_t)grid_put(&vis, cur->crow, (int)c, s + i, uni_len(s, n, i), uni_width(s, n, i) == 2 ? 2 : 1, PAINT_PRE);
	return &vis;
}

static void wl_present(const Grid *cur, Grid *shown)
{
	/* a grid from before the last configure waits for its KEY_RESIZE */
	if (!wl_ready() || cur->rows * cell_h > bh || cur->cols * cell_w > bw) return;
	int all = shown->rows != cur->rows || shown->cols != cur->cols;
	if (all) {                    /* also after a suspend: redraw both buffers whole */
		if (grid_resize(shown, cur->rows, cur->cols)) return;
		for (int i = 0; i < NBUF; i++) grid_free(&bufs[i].g);
	}
	if ((pw != bw || ph != bh) && bufs_make()) return;
	pw = bw;
	ph = bh;
	struct buf *b = bufs[0].busy ? &bufs[1] : &bufs[0];
	const Grid *g = overlay(cur);
	if (b->g.rows != g->rows || b->g.cols != g->cols) {
		if (grid_resize(&b->g, g->rows, g->cols)) return;
		paint_clear(b->px, bw, bw, bh);
		all = 1;
	}
	/* the damage is the rows that differ from the screen, which shows the
	   other buffer when b is not the one last committed: a row b holds
	   already may still need its damage */
	const struct buf *o = !all && on && on->g.rows == g->rows && on->g.cols == g->cols ? on : NULL;
	unsigned char dirty[g->rows];
	paint(b->px, bw, g, &b->g, all, dirty);
	for (int r = 0, l, h; !all && r < g->rows; r++)
		if (!o || (o == b ? dirty[r] : paint_span(g, r, &o->g, r, &l, &h) > 0))
			wl_surface_damage_buffer(surf, 0, r * cell_h, g->cols * cell_w, cell_h);
	if (all) wl_surface_damage_buffer(surf, 0, 0, bw, bh);
	wl_callback_add_listener(frame = wl_surface_frame(surf), &frame_l, NULL);
	waiting = 1;
	wl_surface_attach(surf, b->b, 0, 0);
	wl_surface_commit(surf);
	b->busy = 1;
	on = b;
	if (ti_r != g->crow || ti_c != g->ccol) {
		ti_r = g->crow;
		ti_c = g->ccol;
		ti_cursor();
	}
	wl_display_flush(dpy);
	memcpy(shown->c, cur->c, sizeof *cur->c * (size_t)cur->rows * (size_t)cur->cols);
	shown->crow = cur->crow;
	shown->ccol = cur->ccol;
}

static void wl_suspend(void) {}      /* a window has no terminal to give back */

static void wl_mouse(int *row, int *col) { *row = mrow; *col = mcol; }
static void wl_cell(int *w, int *h) { *w = cell_w; *h = cell_h; }   /* thread 0's, agreed */

const Backend backend_wl = {
	wl_open, wl_close, wl_fds, wl_input, wl_size, wl_ready,
	wl_present, wl_suspend, wl_clip_set, wl_clip_get, wl_mouse, wl_cell
};
