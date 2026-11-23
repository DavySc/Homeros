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
#include <stdint.h>
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
#include "xdg-activation-v1-client-protocol.h"
#include "xdg-toplevel-tag-v1-client-protocol.h"
#include "backend.h"
#include "loop.h"
#include "keys.h"
#include "uni.h"
#include "font.h"
#include "image.h"

/* FONT_PX: pixels per em at scale 1 (hypothesis: Davy's size is not recorded);
   ATTR_PRE: the preedit, underlined (backend-private, after grid.h's attrs);
   ROW: the most bytes and glyphs one row shapes (4 bytes a cell) */
enum { FONT_PX = 16, NBUF = 2, KQ = 1024, PRE = 256, ATTR_PRE = ATTR_IMAGE + 1, ROW = 4096 };
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

static struct xdg_activation_v1 *act;
static struct xdg_toplevel_tag_manager_v1 *tagm;
static struct wl_keyboard *kbd;
static struct wl_pointer *ptr;
static struct wp_cursor_shape_device_v1 *shape;
static struct zwp_text_input_v3 *ti;
static struct wl_data_device *dd;

static int cell_w, cell_h, cell_b;  /* the font's cell and baseline, in pixels */
/* a size already open is used again at once (font.h) */
static int font(int px)
{
	if (font_open(FAMILY, px)) return -1;
	font_cell(&cell_w, &cell_h, &cell_b);
	return 0;
}
static struct wl_registry *reg;

/* An OS window: one per tab page.  Each has its own scale (its monitor's),
   so its own font size, and its own frame callback (Frames.tla "ok"). */
static struct win {
	int id;                         /* backend.h's: the tab page's */
	struct wl_surface *surf;
	struct xdg_surface *xsurf;
	struct xdg_toplevel *top;
	struct wp_viewport *view;
	struct wp_fractional_scale_v1 *fscale;
	struct wl_callback *frame;      /* a frame callback is due */
	int lw, lh, nlw, nlh;           /* logical size, from configure; the configure in progress */
	int scale;                      /* in 120ths, fractional-scale-v1's unit */
	int px, cw, ch;                 /* the font's size at that scale, and its cell */
	int bw, bh, configured;         /* buffer size in pixels */
	struct buf {
		struct wl_buffer *b;
		uint32_t *px;
		int busy;
		Grid g;                 /* what this buffer shows */
	} bufs[NBUF], *on;              /* on: the buffer last committed, on screen */
	void *pool_mem;
	size_t pool_size;
	int pw, ph;                     /* size of the buffers in the pool */
	struct win *next;
} *wins, *kwin, *pwin, *twin;       /* the keyboard's, the pointer's and the input method's */
static int stride;                  /* the buffer drawn into: its width in pixels */
static Grid vis;                    /* cur with the preedit, when there is one */

static struct win *win_of(int id) { struct win *w = wins; while (w && w->id != id) w = w->next; return w; }
static struct win *win_at(struct wl_surface *f) { struct win *w = wins; while (w && w->surf != f) w = w->next; return w; }
static int id_of(const struct win *w) { return w ? w->id : -1; }

static int kq[KQ][2], kh, kn;       /* keys for wl_input, with their window */
static void push(int k, const struct win *w)
{
	if (kn < KQ) { int *q = kq[(kh + kn++) % KQ]; q[0] = k; q[1] = id_of(w); }
}
static void push_utf8(const char *s)
{
	size_t n = strlen(s);
	for (size_t i = 0; i < n; i += uni_len((const unsigned char *)s, n, i))
		push((int)uni_cp((const unsigned char *)s, n, i), twin);
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

/* The keys held on entering are not typed: the press of SPC 5 o's o went
   to the window left, and its release comes here, where no press is
   repeating (measured under sway and Wine, research/frames-os.md).  A
   window entered after another of ours is a focus change (KEY_FOCUS);
   the same one back from another program is none, so its insert mode
   stays. */
static int focused = -1;            /* the last of ours the keyboard was in */
static void kb_enter(void *d, struct wl_keyboard *k, uint32_t s, struct wl_surface *f, struct wl_array *a)
{
	(void)d; (void)k; (void)a;
	serial = s;
	if (!(kwin = win_at(f)) || kwin->id == focused) return;
	focused = kwin->id;
	push(KEY_FOCUS, kwin);
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
	if (c) push(c, kwin);
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
	if (!pwin) return;
	mrow = my / pwin->ch;
	mcol = mx / pwin->cw;
	push(k, pwin);
}
static void pt_motion(void *d, struct wl_pointer *p, uint32_t t, wl_fixed_t x, wl_fixed_t y)
{
	(void)d; (void)p; (void)t;
	if (!pwin) return;
	mx = wl_fixed_to_int(x) * pwin->scale / 120;
	my = wl_fixed_to_int(y) * pwin->scale / 120;
}
static void pt_enter(void *d, struct wl_pointer *p, uint32_t s, struct wl_surface *f, wl_fixed_t x, wl_fixed_t y)
{
	pwin = win_at(f);
	if (shape) wp_cursor_shape_device_v1_set_shape(shape, s, WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_TEXT);
	pt_motion(d, p, 0, x, y);
}
static void pt_leave(void *d, struct wl_pointer *p, uint32_t s, struct wl_surface *f) { (void)d; (void)p; (void)s; (void)f; pwin = NULL; }
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
	if (finger && pwin && a == WL_POINTER_AXIS_VERTICAL_SCROLL) scroll(v, 3 * pwin->ch * 256 * 120 / pwin->scale);   /* in wl_fixed's 1/256 */
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
	if (!ti_on || !twin) return;
	int cw = twin->cw * 120 / twin->scale, ch = twin->ch * 120 / twin->scale;
	zwp_text_input_v3_set_cursor_rectangle(ti, ti_c * cw, ti_r * ch, cw, ch);
	zwp_text_input_v3_commit(ti);
}
static void ti_enter(void *d, struct zwp_text_input_v3 *t, struct wl_surface *f)
{
	(void)d;
	twin = win_at(f);
	ti_r = ti_c = -1;             /* told again at the next frame */
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
	if (pre[0]) { pre[0] = 0; push(KEY_RESIZE, twin); }
	twin = NULL;
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
	if (strcmp(pre, pre_next)) { memcpy(pre, pre_next, sizeof pre); push(KEY_RESIZE, twin); }
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

static void top_configure(void *d, struct xdg_toplevel *t, int32_t w, int32_t h, struct wl_array *st)
{
	struct win *x = d;
	(void)t; (void)st;
	if (w > 0) x->nlw = w;         /* 0: the client chooses, so keep ours */
	if (h > 0) x->nlh = h;
}
static void top_close(void *d, struct xdg_toplevel *t) { (void)t; push(KEY_CLOSE, d); }
static const struct xdg_toplevel_listener top_l = { .configure = top_configure, .close = top_close };

/* the first size is read with wl_size, so only a later one is a key */
static void resize(struct win *w)
{
	w->bw = (w->lw * w->scale + 60) / 120;   /* fractional-scale-v1: round half away from zero */
	w->bh = (w->lh * w->scale + 60) / 120;
	if (w->view) wp_viewport_set_destination(w->view, w->lw, w->lh);
	if (w->configured) push(KEY_RESIZE, w);
}

static void xs_configure(void *d, struct xdg_surface *x, uint32_t s)
{
	struct win *w = d;
	xdg_surface_ack_configure(x, s);
	if (!w->configured || w->nlw != w->lw || w->nlh != w->lh) {
		w->lw = w->nlw;
		w->lh = w->nlh;
		resize(w);
	}
	w->configured = 1;
}
static const struct xdg_surface_listener xs_l = { xs_configure };

static void fs_scale(void *d, struct wp_fractional_scale_v1 *f, uint32_t s)
{
	struct win *w = d;
	int px = (FONT_PX * (int)s + 60) / 120;
	(void)f;
	if ((int)s == w->scale || font(px)) return;
	w->scale = (int)s;
	w->px = px;
	w->cw = cell_w;
	w->ch = cell_h;
	resize(w);
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
	else BIND(act, xdg_activation_v1_interface, 1);
	else BIND(tagm, xdg_toplevel_tag_manager_v1_interface, 1);
#undef BIND
}
static void reg_remove(void *d, struct wl_registry *r, uint32_t id) { (void)d; (void)r; (void)id; }
static const struct wl_registry_listener reg_l = { reg_add, reg_remove };

/* ---- drawing ---- */

static void buf_release(void *d, struct wl_buffer *b) { (void)b; ((struct buf *)d)->busy = 0; }
static const struct wl_buffer_listener buf_l = { buf_release };

static void bufs_free(struct win *w)
{
	for (struct buf *b = w->bufs; b < w->bufs + NBUF; b++) {
		if (b->b) wl_buffer_destroy(b->b);
		b->b = NULL;
		b->busy = 0;
		grid_free(&b->g);
	}
	if (w->pool_mem) munmap(w->pool_mem, w->pool_size);
	w->pool_mem = NULL;
	w->pw = w->ph = 0;
}

/* Buffers of the current size; a busy one is destroyed too, which the
   protocol allows (the compositor keeps the contents it shows). */
static int bufs_make(struct win *w)
{
	bufs_free(w);
	size_t one = (size_t)w->bw * (size_t)w->bh * 4;
	int fd = memfd_create("homeros", MFD_CLOEXEC);
	if (fd < 0) return -1;
	w->pool_size = one * NBUF;
	w->pool_mem = ftruncate(fd, (off_t)w->pool_size) ? MAP_FAILED : mmap(NULL, w->pool_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (w->pool_mem == MAP_FAILED) { close(fd); w->pool_mem = NULL; return -1; }
	struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, (int32_t)w->pool_size);
	for (int i = 0; i < NBUF; i++) {
		w->bufs[i].px = (uint32_t *)((char *)w->pool_mem + one * (size_t)i);
		w->bufs[i].b = wl_shm_pool_create_buffer(pool, (int32_t)(one * (size_t)i), w->bw, w->bh, w->bw * 4, WL_SHM_FORMAT_XRGB8888);
		wl_buffer_add_listener(w->bufs[i].b, &buf_l, &w->bufs[i]);
	}
	wl_shm_pool_destroy(pool);
	close(fd);
	return 0;
}

/* The window's defaults, Catppuccin Mocha like ref/theme.c's faces: base,
   text, and surface1 (catppuccin/nvim's LineNr) for ATTR_DIM */
enum { BG = 0x1e1e2e, FG = 0xcdd6f4, DIM = 0x45475a };
static uint32_t rgb(const unsigned char c[3]) { return (uint32_t)c[0] << 16 | (uint32_t)c[1] << 8 | c[2]; }

/* Callers keep rectangles inside the buffer: cells are, by wl_size. */
static void fill(uint32_t *px, int x0, int y0, int w, int h, uint32_t c)
{
	if (w <= 0 || h <= 0) return;
	uint32_t *p = px + y0 * stride + x0;
	for (int i = 0; i < w; i++) p[i] = c;
	for (int y = 1; y < h; y++) memcpy(p + y * stride, p, (size_t)w * sizeof *p);   /* libc's copy is vectorized (build B) */
}

/* src premultiplied, of alpha a, over dst */
static uint32_t over(uint32_t dst, uint32_t src, unsigned a)
{
	uint32_t r = 0;
	for (int s = 0; s < 24; s += 8) r |= ((src >> s & 0xff) + ((dst >> s & 0xff) * (255 - a) + 127) / 255) << s;
	return r;
}

/* bg + (fg - bg) * a / 255 per channel, rounded, red and blue in one
   multiply (build B): each product fits its 16 bits, and
   (x + 128 + ((x + 128) >> 8)) >> 8 is x / 255 rounded for x up to
   255 * 255 (Blinn, "Three wrongs make a right"); equal to dividing each
   channel by 255 for every bg, fg and a (proved: exhaustive, 33,554,432 cases) */
static uint32_t mix(uint32_t bg, uint32_t fg, unsigned a)
{
	uint32_t rb = (bg & 0xff00ff) * (255 - a) + (fg & 0xff00ff) * a + 0x800080;
	uint32_t g = (bg >> 8 & 0xff) * (255 - a) + (fg >> 8 & 0xff) * a + 0x80;
	rb = (rb + (rb >> 8 & 0xff00ff)) >> 8 & 0xff00ff;
	g = (g + (g >> 8)) >> 8;
	return rb | g << 8;
}

/* The colours of cell c of row r, as ref/tty.c: the attr, then the face's
   colours, reversed last (as under the cursor, cur); bold and italic need
   fonts font.h does not open */
static const Face *colours(const Grid *g, int r, int c, int cur, uint32_t *fg, uint32_t *bg, const Face **u)
{
	const Cell *x = &g->c[r * g->cols + c];
	const Face *b, *f = grid_face(x, &b, u);
	uint32_t t = *fg = f->style & FACE_FG ? rgb(f->fg) : x->attr == ATTR_DIM ? DIM : FG;
	*bg = b->style & FACE_BG ? rgb(b->bg) : BG;
	if (x->attr == ATTR_REV || cur) { *fg = *bg; *bg = t; }
	return f;
}

static int blank(const Cell *x) { return x->len == 1 && x->ch[0] == ' '; }

/* Draws cells lo to hi of row r of g into px: their backgrounds, then the
   cells shaped as one run, since colour does not change the font (kitty
   splits runs by font only).  A glyph wider than its cell (73 of Mathlib's
   295 symbols, reviews/package-7.md) spills into the blank cells after it,
   as foot's overflowing glyphs do, and a ligature's glyph ("->", JetBrains
   Mono's " /\ ") reaches back over its word, as in foot and kitty.  A glyph
   is cut at its word's start, which span never splits, so a partial redraw
   draws what a whole one does. */
static void draw_span(uint32_t *px, const Grid *g, int r, int lo, int hi)
{
	static unsigned char s[ROW];
	static unsigned short col[ROW];     /* the cell of each byte of s */
	static Placed pl[ROW];
	const Cell *x = &g->c[r * g->cols];
	int y0 = r * cell_h, ye = y0 + cell_h, n = 0;
	uint32_t fg, bg;
	if (lo) { s[n] = ' '; col[n++] = (unsigned short)(lo - 1); }   /* span starts after a blank: shape with it, as a whole row */
	for (int c = lo; c < hi; c++) {
		if (!x[c].len) continue;   /* right half: drawn with its left half */
		int w = c + 1 < g->cols && !x[c + 1].len ? 2 : 1;
		const Face *u, *f = colours(g, r, c, r == g->crow && c == g->ccol, &fg, &bg, &u);
		uint32_t sp = u->style & FACE_SP ? rgb(u->sp) : fg;
		fill(px, c * cell_w, y0, w * cell_w, cell_h, bg);
		if (x[c].attr == ATTR_IMAGE) { image_draw(px + y0 * stride + c * cell_w, stride, &x[c]); continue; }   /* thread 0's, agreed */
		if (u->style & FACE_UNDERCURL)
			for (int i = c * cell_w; i < (c + w) * cell_w; i++) fill(px, i, ye - 1 - grid_curl(i, cell_h), 1, 1, sp);
		else if (x[c].attr == ATTR_PRE || u->style & FACE_UNDERLINE) fill(px, c * cell_w, ye - 1, w * cell_w, 1, sp);
		if (f->style & FACE_STRIKE) fill(px, c * cell_w, y0 + cell_h / 2, w * cell_w, 1, fg);
		const unsigned char *t = grid_text(&x[c]);
		for (int i = 0; i < x[c].len && n < ROW; i++) { s[n] = t[i]; col[n++] = (unsigned short)c; }
	}
	for (int i = 0, m = font_shape(s, (size_t)n, pl, ROW); i < m; i++) {
		const Glyph *gl = pl[i].g;
		int c = col[pl[i].cluster], e = c + 1;
		while (e < hi && (!x[e].len || blank(&x[e]))) e++;
		int gx = c * cell_w + pl[i].x + gl->left, gy = y0 + cell_b - pl[i].y - gl->top, xe = e * cell_w;
		int w = c;
		while (w > 0 && !blank(&x[w - 1])) w--;
		int ax = gx < w * cell_w ? w * cell_w - gx : 0, zx = gx + gl->w > xe ? xe - gx : gl->w;
		int ay = gy < y0 ? y0 - gy : 0, zy = gy + gl->h > ye ? ye - gy : gl->h;
		/* the part over each earlier cell of its word in that cell's colours,
		   as kitty colours a ligature per cell, the rest in its own (a spill,
		   as foot's); each part reversed only where its cell is the cursor */
		for (int j0 = ax, j1; j0 < zx; j0 = j1) {
			int k = (gx + j0) / cell_w, m = (k + 1) * cell_w - gx;
			j1 = m < zx ? m : zx;
			k -= !x[k].len;   /* a right half: its left half's */
			const Face *u;
			colours(g, r, k < c ? k : c, r == g->crow && k == g->ccol, &fg, &bg, &u);
			for (int y = ay; y < zy; y++) {
				uint32_t *p = px + (gy + y) * stride + gx;
				if (gl->color) {   /* premultiplied BGRA, the buffer's own byte order */
					const unsigned char *q = gl->px + (size_t)(y * gl->w) * 4;
					for (int j = j0; j < j1; j++) {
						uint32_t v;
						memcpy(&v, q + 4 * j, 4);   /* the cache does not align it */
						unsigned a = v >> 24;
						if (a) p[j] = a == 255 ? v : over(p[j], v, a);
					}
					continue;
				}
				const unsigned char *a = gl->px + y * gl->w;
				for (int j = j0; j < j1; j++)
					if (a[j]) p[j] = a[j] == 255 ? fg : mix(p[j], fg, a[j]);
			}
		}
	}
}

/* Whether cell c of row r of a shows what cell c of row q of b shows */
static int cell_eq(const Grid *a, int r, const Grid *b, int q, int c)
{
	return (a->crow == r && a->ccol == c) == (b->crow == q && b->ccol == c)
		&& grid_cell_eq(&a->c[r * a->cols + c], &b->c[q * b->cols + c]);
}

/* The cells of row r of g that differ from row q of b, as *lo to *hi (only
   these are drawn, as Neovim's grid_put_linebuf sends only them), widened
   on each side to the start of a word after a blank in both rows: no spill
   comes in from the left, and the span's own stop at its right edge.  How
   many cells, 0 if none differ. */
static int span(const Grid *g, int r, const Grid *b, int q, int *lo, int *hi)
{
	int n = g->cols, l = 0, h = n;
	const Cell *x = &g->c[r * n], *y = &b->c[q * n];
	while (l < n && cell_eq(g, r, b, q, l)) l++;
	if (l == n) return *lo = *hi = 0;
	while (h > l && cell_eq(g, r, b, q, h - 1)) h--;
#define EDGE(c) (blank(&x[(c) - 1]) && blank(&y[(c) - 1]) && !blank(&x[c]) && !blank(&y[c]))
	while (l > 0 && !EDGE(l)) l--;
	while (h < n && !EDGE(h)) h++;
#undef EDGE
	*lo = l;
	*hi = h;
	return h - l;
}

/* A hash of row r's text (FNV-1a over what grid_cell_eq compares), without
   the number column (ATTR_DIM), which relative numbers change on a scroll,
   and the background layer, which the cursor line keeps on its row */
static unsigned long row_hash(const Grid *g, int r)
{
	unsigned long h = 2166136261u;
	for (const Cell *x = &g->c[r * g->cols], *e = x + g->cols; x < e; x++) {
		if (x->attr == ATTR_DIM) continue;
		h = (h ^ x->len) * 16777619u;
		h = (h ^ x->attr) * 16777619u;
		h = (h ^ x->face) * 16777619u;
		for (int i = 0; i < x->len && i < 4; i++) h = (h ^ x->ch[i]) * 16777619u;   /* a long cluster: its number, as grid_cell_eq */
	}
	return h;
}

/* How far the rows moved since b was drawn: the move that most changed rows
   of g have, among b's rows, as Emacs's redisplay finds a scroll by row
   hashes (dispnew.c, scrolling_window); 0 if none.  The core sends whole
   grids, not scroll events.  A hash only picks the move: what is drawn
   comes from span. */
static int shift(const Grid *g, const Grid *b)
{
	int n = g->rows, best = 0, most = 0;
	unsigned long hg[n], hb[n];
	for (int r = 0; r < n; r++) { hg[r] = row_hash(g, r); hb[r] = row_hash(b, r); }
	for (int d = 1; d < n; d++)
		for (int k = d; k >= -d; k -= 2 * d) {
			int m = 0;
			for (int r = k < 0 ? -k : 0; r < n && r + k < n; r++) m += hg[r] != hb[r] && hg[r] == hb[r + k];
			if (m > most) { most = m; best = k; }
		}
	return best;
}

/* ---- backend.h ---- */

static void frame_done(void *d, struct wl_callback *cb, uint32_t t)
{
	(void)t;
	wl_callback_destroy(cb);
	((struct win *)d)->frame = NULL;
}
static const struct wl_callback_listener frame_l = { frame_done };

static void win_free(struct win *w)
{
	bufs_free(w);
	if (w->frame) wl_callback_destroy(w->frame);
	if (w->top) xdg_toplevel_destroy(w->top);
	if (w->xsurf) xdg_surface_destroy(w->xsurf);
	if (w->view) wp_viewport_destroy(w->view);
	if (w->fscale) wp_fractional_scale_v1_destroy(w->fscale);
	wl_surface_destroy(w->surf);
	if (kwin == w) kwin = NULL;
	if (pwin == w) pwin = NULL;
	if (twin == w) twin = NULL;
	free(w);
}

static void wl_close(void)
{
	while (wins) {
		struct win *w = wins;
		wins = w->next;
		win_free(w);
	}
	if (sel) wl_data_offer_destroy(sel);
	if (dnd) wl_data_offer_destroy(dnd);
	if (src) wl_data_source_destroy(src);
	if (dd) wl_data_device_destroy(dd);
	if (ti) zwp_text_input_v3_destroy(ti);
	if (shape) wp_cursor_shape_device_v1_destroy(shape);
	if (ptr) wl_pointer_release(ptr);
	if (kbd) wl_keyboard_release(kbd);
	if (tagm) xdg_toplevel_tag_manager_v1_destroy(tagm);
	if (act) xdg_activation_v1_destroy(act);
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
	sel = dnd = NULL; src = NULL; dd = NULL; ti = NULL; shape = NULL; ptr = NULL; kbd = NULL;
	tagm = NULL; act = NULL; vpr = NULL; fsm = NULL; csm = NULL;
	tim = NULL; ddm = NULL; seat = NULL; wm = NULL; shm = NULL; comp = NULL; reg = NULL; dpy = NULL;
	xst = NULL; keymap = NULL; xctx = NULL; rfd = -1; clip = got = NULL;
	gotn = 0;
	ti_on = kn = 0;
	focused = -1;
	rkey = 0;
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
	return 0;
}

/* A new window takes the size and scale of the one with the keyboard:
   Hyprland opens it on that window's monitor (hypothesis), and its
   fractional scale comes only after it is shown */
static int wl_win_open(int id)
{
	const struct win *o = kwin ? kwin : wins;
	struct win *w = calloc(1, sizeof *w);
	if (!w || !(w->surf = wl_compositor_create_surface(comp))) { free(w); return -1; }
	if (o) { w->lw = o->lw; w->lh = o->lh; w->scale = o->scale; w->px = o->px; w->cw = o->cw; w->ch = o->ch; }
	else { w->lw = 800; w->lh = 600; w->scale = 120; w->px = FONT_PX; w->cw = cell_w; w->ch = cell_h; }
	w->id = id;
	w->next = wins;
	wins = w;
	if (focused < 0) focused = id;   /* the first window's first focus is no change */
	if (fsm && vpr) {
		wp_fractional_scale_v1_add_listener(w->fscale = wp_fractional_scale_manager_v1_get_fractional_scale(fsm, w->surf), &fs_l, w);
		w->view = wp_viewporter_get_viewport(vpr, w->surf);
	}
	xdg_surface_add_listener(w->xsurf = xdg_wm_base_get_xdg_surface(wm, w->surf), &xs_l, w);
	xdg_toplevel_add_listener(w->top = xdg_surface_get_toplevel(w->xsurf), &top_l, w);
	xdg_toplevel_set_title(w->top, "homeros");
	xdg_toplevel_set_app_id(w->top, "homeros");
	if (tagm) xdg_toplevel_tag_manager_v1_set_toplevel_tag(tagm, w->top, "homeros-main");
	wl_surface_commit(w->surf);
	w->nlw = w->lw;
	w->nlh = w->lh;
	while (!w->configured)
		if (wl_display_dispatch(dpy) < 0) return -1;
	return 0;
}

static void wl_win_close(int id)
{
	struct win **p = &wins;
	while (*p && (*p)->id != id) p = &(*p)->next;
	if (!*p) return;
	struct win *w = *p;
	*p = w->next;
	win_free(w);
	wl_display_flush(dpy);
}

/* SPC 5 o: xdg-activation with the serial of the last key, from the
   window that has the keyboard.  A sync sent after the activate answers
   once the compositor has handled it (Hyprland focuses at once, read in
   its source), so KEY_RAISED comes after the focus it gives (Focus.tla);
   tokens and syncs are answered in order, so the raises are too. */
static void raised(void *d, struct wl_callback *cb, uint32_t t) { (void)d; (void)t; wl_callback_destroy(cb); push(KEY_RAISED, NULL); }
static const struct wl_callback_listener raised_l = { raised };
static void token_done(void *d, struct xdg_activation_token_v1 *tk, const char *token)
{
	struct win *w = win_of((int)(intptr_t)d);
	if (w) xdg_activation_v1_activate(act, token, w->surf);
	xdg_activation_token_v1_destroy(tk);
	wl_callback_add_listener(wl_display_sync(dpy), &raised_l, NULL);
}
static const struct xdg_activation_token_v1_listener token_l = { token_done };
static void wl_raise(int id)
{
	if (!act) { push(KEY_RAISED, NULL); arm(); return; }
	struct xdg_activation_token_v1 *tk = xdg_activation_v1_get_activation_token(act);
	xdg_activation_token_v1_add_listener(tk, &token_l, (void *)(intptr_t)id);
	if (seat) xdg_activation_token_v1_set_serial(tk, serial, seat);
	if (kwin) xdg_activation_token_v1_set_surface(tk, kwin->surf);
	xdg_activation_token_v1_commit(tk);
	wl_display_flush(dpy);
}

static int wl_fds(int fds[2])
{
	fds[0] = wl_display_get_fd(dpy);
	fds[1] = rfd;
	return 2;
}

static int wl_input(int *out, int *from, int n)
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
		for (long long j = 0, k = key_of(rkey); j < c && kn < KQ; j++) push((int)k, kwin);
		rnext += c * per;
	}
	int i = 0;
	for (; i < n && kn; i++, kn--, kh = (kh + 1) % KQ) out[i] = kq[kh][0], from[i] = kq[kh][1];
	arm();
	return i;
}

static void wl_size(int id, int *rows, int *cols)
{
	const struct win *w = win_of(id);
	*rows = w && w->bh / w->ch > 0 ? w->bh / w->ch : 1;
	*cols = w && w->bw / w->cw > 0 ? w->bw / w->cw : 1;
}

static int wl_ready(int id)
{
	const struct win *w = win_of(id);
	if (!w || !w->configured || w->frame) return 0;
	if (w->pw != w->bw || w->ph != w->bh) return 1;
	for (int i = 0; i < NBUF; i++) if (!w->bufs[i].busy) return 1;
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
		c += (size_t)grid_put(&vis, cur->crow, (int)c, s + i, uni_len(s, n, i), uni_width(s, n, i) == 2 ? 2 : 1, ATTR_PRE);
	return &vis;
}

static void wl_present(int id, const Grid *cur, Grid *shown)
{
	struct win *w = win_of(id);
	/* a grid from before the last configure waits for its KEY_RESIZE */
	if (!wl_ready(id) || font(w->px) || cur->rows * cell_h > w->bh || cur->cols * cell_w > w->bw) return;
	int all = shown->rows != cur->rows || shown->cols != cur->cols;
	if (all) {                    /* also after a suspend: redraw both buffers whole */
		if (grid_resize(shown, cur->rows, cur->cols)) return;
		for (int i = 0; i < NBUF; i++) grid_free(&w->bufs[i].g);
	}
	if ((w->pw != w->bw || w->ph != w->bh) && bufs_make(w)) return;
	w->pw = w->bw;
	w->ph = w->bh;
	stride = w->bw;
	struct buf *b = w->bufs[0].busy ? &w->bufs[1] : &w->bufs[0];
	const Grid *g = w == twin ? overlay(cur) : cur;   /* the preedit is in the input method's window */
	if (b->g.rows != g->rows || b->g.cols != g->cols) {
		if (grid_resize(&b->g, g->rows, g->cols)) return;
		fill(b->px, 0, 0, w->bw, w->bh, BG);
		all = 1;
	}
	/* rows a scroll moved are copied, not shaped again (shaping all 60 rows
	   takes p99 1.91 ms of the 2 ms frame, reviews/package-7.md), and of a
	   row only the cells that changed are drawn.  Copying in the direction
	   of the move reads each source before it is drawn over. */
	int k = all ? 0 : shift(g, &b->g);
	size_t row = (size_t)cell_h * (size_t)w->bw;
	/* the damage is the rows that differ from the screen, which shows the
	   other buffer when b is not the one last committed: a row b holds
	   already may still need its damage */
	const struct buf *o = !all && w->on && w->on->g.rows == g->rows && w->on->g.cols == g->cols ? w->on : NULL;
	for (int i = 0; i < g->rows; i++) {
		int r = k < 0 ? g->rows - 1 - i : i, lo = 0, hi = g->cols, l, h, d = 1;
		if (!all) {
			int n = span(g, r, &b->g, r, &lo, &hi);
			if (o) d = o == b ? n : span(g, r, &o->g, r, &l, &h);
			if (d) wl_surface_damage_buffer(w->surf, 0, r * cell_h, g->cols * cell_w, cell_h);
			if (!n) continue;
			if (k && r + k >= 0 && r + k < g->rows && span(g, r, &b->g, r + k, &l, &h) < n) {
				memcpy(b->px + (size_t)r * row, b->px + (size_t)(r + k) * row, row * sizeof *b->px);
				lo = l;
				hi = h;
			}
		}
		if (lo < hi) draw_span(b->px, g, r, lo, hi);
		memcpy(&b->g.c[r * g->cols], &g->c[r * g->cols], sizeof *g->c * (size_t)g->cols);
	}
	b->g.crow = g->crow;
	b->g.ccol = g->ccol;
	if (all) wl_surface_damage_buffer(w->surf, 0, 0, w->bw, w->bh);
	wl_callback_add_listener(w->frame = wl_surface_frame(w->surf), &frame_l, w);
	wl_surface_attach(w->surf, b->b, 0, 0);
	wl_surface_commit(w->surf);
	b->busy = 1;
	w->on = b;
	if (w == twin && (ti_r != g->crow || ti_c != g->ccol)) {
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
static void wl_cell(int id, int *w, int *h)   /* thread 0's, agreed */
{
	const struct win *x = win_of(id);
	*w = x ? x->cw : 0;
	*h = x ? x->ch : 0;
}

const Backend backend_wl = {
	wl_open, wl_close, wl_win_open, wl_win_close, wl_raise, wl_fds, wl_input, wl_size, wl_ready,
	wl_present, wl_suspend, wl_clip_set, wl_clip_get, wl_mouse, wl_cell
};
