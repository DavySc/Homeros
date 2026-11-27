/* Wayland backend: an xdg-shell window drawn on the CPU into wl_shm buffers,
   as foot does.  Frames follow Present.tla's "framecb" policy: draw only
   after the compositor's frame callback and into a buffer it has released.
   Keys come from xkbcommon with client-side repeat on a timerfd, text from
   an input method through text-input-v3, the "+ register through
   wl_data_device; the pointer gets the text cursor (cursor-shape-v1) and
   gives left clicks and the wheel as keys; the buffer is drawn at the fractional scale the compositor prefers
   (fractional-scale-v1 with a viewport); a wallpaper (image.h) is a
   subsurface below it.  These are the protocols Hyprland
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
static struct wl_subcompositor *subc;

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
	int px;                         /* the font's size at that scale */
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
	const uint32_t *back;           /* with a wallpaper, a clear row after the buffers (paint.h) */
	struct wl_surface *wsurf;       /* the wallpaper's, below surf */
	struct wl_subsurface *wsub;
	struct wp_viewport *wview;
	struct wl_buffer *wbuf;
	unsigned up;                    /* the version of the picture in wbuf */
	int wall;                       /* a wallpaper is behind the text (image.h) */
	struct win *next;
} *wins, *kwin, *pwin, *twin;       /* the keyboard's, the pointer's and the input method's */
static Grid vis;                    /* cur with the preedit, when there is one */

static struct win *win_of(int id) { struct win *w = wins; while (w && w->id != id) w = w->next; return w; }
static struct win *win_at(struct wl_surface *f) { struct win *w = wins; while (w && w->surf != f) w = w->next; return w; }
static int id_of(const struct win *w) { return w ? w->id : -1; }
/* w's font, so its cell is cell_w by cell_h: its size alone says it, and
   a size used again costs nothing (font.h); nonzero without one */
static int use(const struct win *w) { return !w || font(w->px); }

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
	if (use(pwin)) return;
	mrow = my / cell_h;
	mcol = mx / cell_w;
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
	if (finger && a == WL_POINTER_AXIS_VERTICAL_SCROLL && !use(pwin)) scroll(v, 3 * cell_h * 256 * 120 / pwin->scale);   /* in wl_fixed's 1/256 */
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
	if (!ti_on || use(twin)) return;
	int cw = cell_w * 120 / twin->scale, ch = cell_h * 120 / twin->scale;
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
	else BIND(subc, wl_subcompositor_interface, 1);
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
	w->back = NULL;
	w->pw = w->ph = 0;
}

/* n bytes of shared memory as a pool, mapped at *mem; NULL if none */
static struct wl_shm_pool *pool_new(size_t n, void **mem)
{
	int fd = memfd_create("homeros", MFD_CLOEXEC);
	if (fd < 0) return NULL;
	*mem = ftruncate(fd, (off_t)n) ? MAP_FAILED : mmap(NULL, n, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	struct wl_shm_pool *pool = *mem == MAP_FAILED ? NULL : wl_shm_create_pool(shm, fd, (int32_t)n);
	close(fd);
	return pool;
}

/* Buffers of the current size; a busy one is destroyed too, which the
   protocol allows (the compositor keeps the contents it shows). */
static int bufs_make(struct win *w)
{
	bufs_free(w);
	size_t one = (size_t)w->bw * (size_t)w->bh * 4;
	struct wl_shm_pool *pool = pool_new(w->pool_size = one * NBUF + (size_t)w->bw * 4, &w->pool_mem);
	if (!pool) { w->pool_mem = NULL; return -1; }
	w->back = w->wall ? (const uint32_t *)((char *)w->pool_mem + one * NBUF) : NULL;   /* a new memfd is zeros */
	for (int i = 0; i < NBUF; i++) {
		w->bufs[i].px = (uint32_t *)((char *)w->pool_mem + one * (size_t)i);
		w->bufs[i].b = wl_shm_pool_create_buffer(pool, (int32_t)(one * (size_t)i), w->bw, w->bh, w->bw * 4,
		                                         w->wall ? WL_SHM_FORMAT_ARGB8888 : WL_SHM_FORMAT_XRGB8888);
		wl_buffer_add_listener(w->bufs[i].b, &buf_l, &w->bufs[i]);
	}
	wl_shm_pool_destroy(pool);
	return 0;
}

/* The wallpaper (image.h) on a surface of its own below the window's, its
   part that covers the window scaled by the compositor (a viewport), so a
   new size only says where; it shows with the window's next commit (a
   synchronized subsurface).  The text is drawn as without one, moved rows
   copied, into an ARGB buffer clear where the background is the default,
   and the compositor blends the two: a frame costs no more (measured,
   src/image/README.md).  Sets wall. */
static void wall_make(struct win *w)
{
	int iw, ih, src[4];
	unsigned v;
	const uint32_t *px = subc && vpr ? image_wallpaper(w->bw, w->bh, PAINT_BASE, &iw, &ih, src, &v) : NULL;
	if (w->wbuf && (!px || v != w->up)) {
		wl_buffer_destroy(w->wbuf);   /* the compositor keeps what it shows */
		w->wbuf = NULL;
	}
	if (!(w->wall = px != NULL)) {
		if (w->wsurf) {
			wl_surface_attach(w->wsurf, NULL, 0, 0);
			wl_surface_commit(w->wsurf);
		}
		return;
	}
	if (!w->wsurf) {
		w->wsurf = wl_compositor_create_surface(comp);
		w->wsub = wl_subcompositor_get_subsurface(subc, w->wsurf, w->surf);
		wl_subsurface_place_below(w->wsub, w->surf);
		w->wview = wp_viewporter_get_viewport(vpr, w->wsurf);
	}
	if (!w->wbuf) {
		size_t n = (size_t)iw * (size_t)ih * 4;
		void *mem;
		struct wl_shm_pool *pool = pool_new(n, &mem);
		if (!pool) { w->wall = 0; return; }   /* text over the base, rather than no window */
		memcpy(mem, px, n);
		munmap(mem, n);
		w->wbuf = wl_shm_pool_create_buffer(pool, 0, iw, ih, iw * 4, WL_SHM_FORMAT_XRGB8888);
		wl_shm_pool_destroy(pool);
		wl_surface_attach(w->wsurf, w->wbuf, 0, 0);
		wl_surface_damage_buffer(w->wsurf, 0, 0, iw, ih);
		w->up = v;
	}
	wp_viewport_set_source(w->wview, wl_fixed_from_int(src[0]), wl_fixed_from_int(src[1]), wl_fixed_from_int(src[2]), wl_fixed_from_int(src[3]));
	wp_viewport_set_destination(w->wview, w->lw, w->lh);
	wl_surface_commit(w->wsurf);
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
	if (w->wbuf) wl_buffer_destroy(w->wbuf);
	if (w->wview) wp_viewport_destroy(w->wview);
	if (w->wsub) wl_subsurface_destroy(w->wsub);
	if (w->wsurf) wl_surface_destroy(w->wsurf);
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
	if (subc) wl_subcompositor_destroy(subc);
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
	subc = NULL;
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
	if (o) { w->lw = o->lw; w->lh = o->lh; w->scale = o->scale; w->px = o->px; }
	else { w->lw = 800; w->lh = 600; w->scale = 120; w->px = FONT_PX; }
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
	int u = use(w);
	*rows = !u && w->bh / cell_h > 0 ? w->bh / cell_h : 1;
	*cols = !u && w->bw / cell_w > 0 ? w->bw / cell_w : 1;
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
		c += (size_t)grid_put(&vis, cur->crow, (int)c, s + i, uni_len(s, n, i), uni_width(s, n, i) == 2 ? 2 : 1, PAINT_PRE);
	return &vis;
}

static void wl_present(int id, const Grid *cur, Grid *shown)
{
	struct win *w = win_of(id);
	/* a grid from before the last configure waits for its KEY_RESIZE */
	if (!wl_ready(id) || use(w) || cur->rows * cell_h > w->bh || cur->cols * cell_w > w->bw) return;
	int all = shown->rows != cur->rows || shown->cols != cur->cols;
	if (all) {                    /* also after a suspend: redraw both buffers whole */
		if (grid_resize(shown, cur->rows, cur->cols)) return;
		for (int i = 0; i < NBUF; i++) grid_free(&w->bufs[i].g);
	}
	if (w->pw != w->bw || w->ph != w->bh) {
		wall_make(w);
		if (bufs_make(w)) return;
	}
	w->pw = w->bw;
	w->ph = w->bh;
	struct buf *b = w->bufs[0].busy ? &w->bufs[1] : &w->bufs[0];
	const Grid *g = w == twin ? overlay(cur) : cur;   /* the preedit is in the input method's window */
	if (b->g.rows != g->rows || b->g.cols != g->cols) {
		if (grid_resize(&b->g, g->rows, g->cols)) return;
		paint_clear(b->px, w->bw, w->back, 0, w->bw, w->bh);
		all = 1;
	}
	/* the damage is the rows that differ from the screen, which shows the
	   other buffer when b is not the one last committed: a row b holds
	   already may still need its damage */
	const struct buf *o = !all && w->on && w->on->g.rows == g->rows && w->on->g.cols == g->cols ? w->on : NULL;
	unsigned char dirty[g->rows];
	paint(b->px, w->bw, w->back, 0, g, &b->g, all, dirty);
	for (int r = 0, l, h; !all && r < g->rows; r++)
		if (!o || (o == b ? dirty[r] : paint_span(g, r, &o->g, r, &l, &h) > 0))
			wl_surface_damage_buffer(w->surf, 0, r * cell_h, g->cols * cell_w, cell_h);
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
	int u = use(win_of(id));
	*w = u ? 0 : cell_w;
	*h = u ? 0 : cell_h;
}

static void wl_title(int id, const char *s) { struct win *w = win_of(id); if (w) xdg_toplevel_set_title(w->top, s); }

const Backend backend_wl = {
	wl_open, wl_close, wl_win_open, wl_win_close, wl_raise, wl_fds, wl_input, wl_size, wl_ready,
	wl_present, wl_suspend, wl_clip_set, wl_clip_get, wl_mouse, wl_cell, wl_title
};
