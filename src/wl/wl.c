/* Wayland backend (package 8): a window with no toolkit, drawn on the CPU
   into wl_shm buffers, as foot does.  It draws only after the compositor's
   frame callback (Present.tla "framecb": no frame is drawn that is never
   shown, nothing is drawn while hidden), takes the size xdg-shell
   configures and the largest scale of the outputs it is on, repeats keys
   itself (Wayland leaves that to clients), and speaks text-input v1 for the
   IME: the one version both weston and Hyprland offer (weston 16 has no v3). */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/timerfd.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>
#include "backend.h"
#include "font.h"
#include "keys.h"
#include "uni.h"
#include "cursor-shape-v1.h"
#include "text-input-unstable-v1.h"
#include "xdg-shell.h"

/* FONT_PX is a guess at Davy's size (hypothesis: 12 pt at 96 dpi); WHEEL is
   one wheel notch in axis units (15 in weston and wlroots, hypothesis for
   Hyprland); CLIP_MS bounds a paste from a client that never writes. */
#define FAMILY "JetBrains Mono"
enum { FONT_PX = 16, NOUT = 8, KQ = 8192, PRE = 256, WHEEL = 15, CLIP_MS = 1000 };
enum { A_CUR = 0x80, A_PRE = 0x40 };   /* cell attrs only this file draws */

static struct wl_display *dpy;
static struct wl_compositor *comp;
static struct wl_shm *shm;
static struct xdg_wm_base *wm;
static struct wl_seat *seat;
static struct wl_data_device_manager *ddm;
static struct zwp_text_input_manager_v1 *tim;
static struct wp_cursor_shape_manager_v1 *csm;
static struct wl_surface *surf;
static struct xdg_surface *xsurf;
static struct xdg_toplevel *top;
static struct wl_keyboard *kbd;
static struct wl_pointer *ptr;
static struct wl_data_device *ddev;
static struct zwp_text_input_v1 *ti;
static struct wp_cursor_shape_device_v1 *shape;
static struct xkb_context *xctx;
static struct xkb_keymap *km;
static struct xkb_state *xst;

static struct { struct wl_output *o; int scale, on; } out[NOUT];
static int scale = 1, cw, chh, base;      /* cell in buffer pixels */
static int lw, lh, pw, ph, configured;    /* logical size; configured, pending */
static int framecb, tfd = -1, gone;
static uint32_t serial;                   /* of the last key or button: clipboard ownership */

static int kq[KQ], kh, kn;                /* keys made by events, not yet read */
static int mrow, mcol;
static double mx, my, wheel;
static int rep_key;                       /* repeating key, 0 none */
static uint32_t rep_code;
static long long rep_next, rep_ns, rep_delay;
static char pre[PRE];                     /* IME preedit */
static int pre_dirty;
static uint32_t ti_serial;
static struct wl_data_source *src;
static char *clip;
static size_t clipn;
static struct wl_data_offer *offer;       /* the selection, if it has text */

/* Each buffer remembers the cells it holds, so a buffer two frames old gets
   the cells it lacks (not only those that changed since the last frame). */
typedef struct {
	struct wl_buffer *wb;
	uint32_t *px;
	size_t size;
	int w, h, busy, valid;
	Grid g;
} Buf;
static Buf buf[2], *last = buf;
static Grid cur;                          /* the core's last grid, without the overlays */
static Cell *row;                         /* one row with cursor and preedit drawn in */
static unsigned char *run;
static int *col_of;                       /* byte in run -> column */
static Placed *placed;
static int cap;                           /* columns the scratch arrays hold */

static long long now(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000000000LL + t.tv_nsec;
}

static void push(int k)
{
	if (kn < KQ) kq[(kh + kn++) % KQ] = k;
}

static void push_utf8(const char *s)
{
	size_t n = strlen(s);
	for (size_t i = 0; i < n; i += uni_len((const unsigned char *)s, n, i))
		push((int)uni_cp((const unsigned char *)s, n, i));
}

/* The timer fires for the next repeat, or at once while keys wait in kq
   (an absolute time of 1 ns is long past): the main loop only calls
   wl_input when an fd is readable, and the socket may not be. */
static void arm(void)
{
	long long at = kn ? 1 : rep_key ? rep_next : 0;
	struct itimerspec t = { .it_value = { (time_t)(at / 1000000000), at % 1000000000 } };
	timerfd_settime(tfd, TFD_TIMER_ABSTIME, &t, NULL);
}

static int key_of(xkb_keysym_t sym, uint32_t cp)
{
	switch (sym) {
	case XKB_KEY_Up: return KEY_UP;
	case XKB_KEY_Down: return KEY_DOWN;
	case XKB_KEY_Left: return KEY_LEFT;
	case XKB_KEY_Right: return KEY_RIGHT;
	case XKB_KEY_BackSpace: return KEY_BS;      /* xkb says 8; terminals and Neovim say 127 */
	case XKB_KEY_Return: case XKB_KEY_KP_Enter: return KEY_ENTER;
	case XKB_KEY_Delete: return 0;              /* no key code for it yet */
	}
	return (int)cp;
}

/* ---- the window ---- */

static void out_geometry(void *d, struct wl_output *o, int32_t x, int32_t y, int32_t w, int32_t h,
                         int32_t sub, const char *mk, const char *mo, int32_t tr)
{ (void)d; (void)o; (void)x; (void)y; (void)w; (void)h; (void)sub; (void)mk; (void)mo; (void)tr; }
static void out_mode(void *d, struct wl_output *o, uint32_t f, int32_t w, int32_t h, int32_t r)
{ (void)d; (void)o; (void)f; (void)w; (void)h; (void)r; }
static void out_done(void *d, struct wl_output *o) { (void)d; (void)o; }

static void rescale(void)
{
	int s = 1;
	for (int i = 0; i < NOUT; i++) if (out[i].on && out[i].scale > s) s = out[i].scale;
	if (s == scale && cw) return;
	scale = s;
	if (font_open(FAMILY, FONT_PX * scale)) return;
	font_cell(&cw, &chh, &base);
	if (surf) wl_surface_set_buffer_scale(surf, scale);
	push(KEY_RESIZE);
}

static void out_scale(void *d, struct wl_output *o, int32_t f)
{
	(void)o;
	out[(intptr_t)d].scale = f;
	rescale();
}
static const struct wl_output_listener out_l = { .geometry = out_geometry, .mode = out_mode, .done = out_done, .scale = out_scale };

static void surf_on(struct wl_output *o, int on)
{
	for (int i = 0; i < NOUT; i++) if (out[i].o == o) out[i].on = on;
	rescale();
}
static void surf_enter(void *d, struct wl_surface *s, struct wl_output *o) { (void)d; (void)s; surf_on(o, 1); }
static void surf_leave(void *d, struct wl_surface *s, struct wl_output *o) { (void)d; (void)s; surf_on(o, 0); }
static const struct wl_surface_listener surf_l = { .enter = surf_enter, .leave = surf_leave };

static void wm_ping(void *d, struct xdg_wm_base *w, uint32_t s) { (void)d; xdg_wm_base_pong(w, s); }
static const struct xdg_wm_base_listener wm_l = { wm_ping };

static void top_configure(void *d, struct xdg_toplevel *t, int32_t w, int32_t h, struct wl_array *st)
{
	(void)d; (void)t; (void)st;
	pw = w; ph = h;
}

static void top_close(void *d, struct xdg_toplevel *t) { (void)d; (void)t; push(KEY_CLOSE); }
static const struct xdg_toplevel_listener top_l = { .configure = top_configure, .close = top_close };

static void xs_configure(void *d, struct xdg_surface *x, uint32_t s)
{
	(void)d;
	xdg_surface_ack_configure(x, s);
	/* 0 leaves the size to us: 80 by 24 cells, the terminal's default */
	int w = pw > 0 ? pw : lw ? lw : 80 * cw / scale, h = ph > 0 ? ph : lh ? lh : 24 * chh / scale;
	if (w != lw || h != lh) { lw = w; lh = h; if (configured) push(KEY_RESIZE); }
	configured = 1;
}
static const struct xdg_surface_listener xs_l = { xs_configure };

/* ---- keys ---- */

static void kb_keymap(void *d, struct wl_keyboard *k, uint32_t f, int fd, uint32_t n)
{
	(void)d; (void)k;
	char *m = f == WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1 ? mmap(NULL, n, PROT_READ, MAP_PRIVATE, fd, 0) : MAP_FAILED;
	close(fd);
	if (m == MAP_FAILED) return;
	struct xkb_keymap *nk = xkb_keymap_new_from_buffer(xctx, m, strnlen(m, n), XKB_KEYMAP_FORMAT_TEXT_V1, 0);
	munmap(m, n);
	if (!nk) return;
	xkb_state_unref(xst);
	xkb_keymap_unref(km);
	km = nk;
	xst = xkb_state_new(km);
}

static void kb_enter(void *d, struct wl_keyboard *k, uint32_t s, struct wl_surface *f, struct wl_array *a)
{
	(void)d; (void)k; (void)f; (void)a;
	serial = s;
	if (ti) {
		zwp_text_input_v1_activate(ti, seat, surf);
		ti_serial = 0;   /* the cursor rectangle goes out with the next frame */
	}
}

static void kb_leave(void *d, struct wl_keyboard *k, uint32_t s, struct wl_surface *f)
{
	(void)d; (void)k; (void)s; (void)f;
	rep_key = 0;
	if (ti) zwp_text_input_v1_deactivate(ti, seat);
}

static void kb_key(void *d, struct wl_keyboard *k, uint32_t s, uint32_t t, uint32_t code, uint32_t st)
{
	(void)d; (void)k; (void)t;
	if (!xst) return;
	if (st != WL_KEYBOARD_KEY_STATE_PRESSED) {
		if (code == rep_code) rep_key = 0;
		return;
	}
	serial = s;
	int key = key_of(xkb_state_key_get_one_sym(xst, code + 8), xkb_state_key_get_utf32(xst, code + 8));
	if (!key) return;
	push(key);
	if (rep_ns && xkb_keymap_key_repeats(km, code + 8)) {
		rep_key = key;
		rep_code = code;
		rep_next = now() + rep_delay;
	}
}

static void kb_mods(void *d, struct wl_keyboard *k, uint32_t s, uint32_t dep, uint32_t lat, uint32_t lock, uint32_t grp)
{
	(void)d; (void)k; (void)s;
	if (xst) xkb_state_update_mask(xst, dep, lat, lock, 0, 0, grp);
}

static void kb_repeat(void *d, struct wl_keyboard *k, int32_t rate, int32_t delay)
{
	(void)d; (void)k;
	rep_ns = rate > 0 ? 1000000000LL / rate : 0;
	rep_delay = delay * 1000000LL;
	if (!rep_ns) rep_key = 0;
}
static const struct wl_keyboard_listener kb_l = { kb_keymap, kb_enter, kb_leave, kb_key, kb_mods, kb_repeat };

/* ---- the pointer: Neovim's mouse=nvi, a click or a wheel notch at a cell ---- */

static void at_pointer(int key)
{
	mrow = (int)(my * scale) / chh;
	mcol = (int)(mx * scale) / cw;
	push(key);
}

static void pt_enter(void *d, struct wl_pointer *p, uint32_t s, struct wl_surface *f, wl_fixed_t x, wl_fixed_t y)
{
	(void)d; (void)p; (void)f;
	mx = wl_fixed_to_double(x);
	my = wl_fixed_to_double(y);
	if (shape) wp_cursor_shape_device_v1_set_shape(shape, s, WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_TEXT);
}
static void pt_leave(void *d, struct wl_pointer *p, uint32_t s, struct wl_surface *f) { (void)d; (void)p; (void)s; (void)f; }
static void pt_motion(void *d, struct wl_pointer *p, uint32_t t, wl_fixed_t x, wl_fixed_t y)
{
	(void)d; (void)p; (void)t;
	mx = wl_fixed_to_double(x);
	my = wl_fixed_to_double(y);
}
static void pt_button(void *d, struct wl_pointer *p, uint32_t s, uint32_t t, uint32_t b, uint32_t st)
{
	(void)d; (void)p; (void)t;
	if (b != 0x110 || st != WL_POINTER_BUTTON_STATE_PRESSED) return;   /* BTN_LEFT */
	serial = s;
	at_pointer(KEY_MOUSE_LEFT);
}
static void pt_axis(void *d, struct wl_pointer *p, uint32_t t, uint32_t axis, wl_fixed_t v)
{
	(void)d; (void)p; (void)t;
	if (axis != WL_POINTER_AXIS_VERTICAL_SCROLL) return;
	/* a touchpad sends small steps: a key per notch's worth */
	for (wheel += wl_fixed_to_double(v); wheel >= WHEEL; wheel -= WHEEL) at_pointer(KEY_WHEEL_DOWN);
	for (; wheel <= -WHEEL; wheel += WHEEL) at_pointer(KEY_WHEEL_UP);
}
static const struct wl_pointer_listener pt_l = { .enter = pt_enter, .leave = pt_leave, .motion = pt_motion,
	.button = pt_button, .axis = pt_axis };

static void seat_caps(void *d, struct wl_seat *s, uint32_t c)
{
	(void)d;
	if (c & WL_SEAT_CAPABILITY_KEYBOARD && !kbd) {
		kbd = wl_seat_get_keyboard(s);
		wl_keyboard_add_listener(kbd, &kb_l, NULL);
	}
	if (c & WL_SEAT_CAPABILITY_POINTER && !ptr) {
		ptr = wl_seat_get_pointer(s);
		wl_pointer_add_listener(ptr, &pt_l, NULL);
		if (csm) shape = wp_cursor_shape_manager_v1_get_pointer(csm, ptr);
	}
}
static void seat_name(void *d, struct wl_seat *s, const char *n) { (void)d; (void)s; (void)n; }
static const struct wl_seat_listener seat_l = { seat_caps, seat_name };

/* ---- the IME ---- */

static void ti_enter(void *d, struct zwp_text_input_v1 *t, struct wl_surface *s) { (void)d; (void)t; (void)s; }
static void ti_leave(void *d, struct zwp_text_input_v1 *t) { (void)d; (void)t; }
static void ti_modmap(void *d, struct zwp_text_input_v1 *t, struct wl_array *m) { (void)d; (void)t; (void)m; }
static void ti_panel(void *d, struct zwp_text_input_v1 *t, uint32_t st) { (void)d; (void)t; (void)st; }
static void ti_preedit(void *d, struct zwp_text_input_v1 *t, uint32_t s, const char *text, const char *commit)
{
	(void)d; (void)t; (void)s; (void)commit;
	size_t n = strlen(text);
	if (n >= PRE) n = uni_prev((const unsigned char *)text, n, PRE);   /* cut at a character */
	memcpy(pre, text, n);
	pre[n] = 0;
	pre_dirty = 1;
}
static void ti_styling(void *d, struct zwp_text_input_v1 *t, uint32_t i, uint32_t n, uint32_t st)
{ (void)d; (void)t; (void)i; (void)n; (void)st; }
static void ti_precursor(void *d, struct zwp_text_input_v1 *t, int32_t i) { (void)d; (void)t; (void)i; }
static void ti_commit(void *d, struct zwp_text_input_v1 *t, uint32_t s, const char *text)
{
	(void)d; (void)t; (void)s;
	pre[0] = 0;
	pre_dirty = 1;
	push_utf8(text);
}
static void ti_cursor(void *d, struct zwp_text_input_v1 *t, int32_t i, int32_t a) { (void)d; (void)t; (void)i; (void)a; }
static void ti_delete(void *d, struct zwp_text_input_v1 *t, int32_t i, uint32_t n) { (void)d; (void)t; (void)i; (void)n; }
/* v1 input methods send the keys they do not compose (Enter, BackSpace) as keysyms */
static void ti_keysym(void *d, struct zwp_text_input_v1 *t, uint32_t s, uint32_t tm, uint32_t sym, uint32_t st, uint32_t m)
{
	(void)d; (void)t; (void)s; (void)tm; (void)m;
	int key = key_of(sym, xkb_keysym_to_utf32(sym));
	if (st == WL_KEYBOARD_KEY_STATE_PRESSED && key) push(key);
}
static void ti_lang(void *d, struct zwp_text_input_v1 *t, uint32_t s, const char *l) { (void)d; (void)t; (void)s; (void)l; }
static void ti_dir(void *d, struct zwp_text_input_v1 *t, uint32_t s, uint32_t dir) { (void)d; (void)t; (void)s; (void)dir; }
static const struct zwp_text_input_v1_listener ti_l = { ti_enter, ti_leave, ti_modmap, ti_panel, ti_preedit,
	ti_styling, ti_precursor, ti_commit, ti_cursor, ti_delete, ti_keysym, ti_lang, ti_dir };

/* ---- the clipboard ---- */

static const char *const MIME[] = { "text/plain;charset=utf-8", "UTF8_STRING", "text/plain" };

static void offer_mime(void *d, struct wl_data_offer *o, const char *m)
{
	(void)d;
	intptr_t best = (intptr_t)wl_data_offer_get_user_data(o);
	for (intptr_t i = 1; i <= 3; i++)
		if (!strcmp(m, MIME[i - 1]) && (!best || i < best)) wl_data_offer_set_user_data(o, (void *)i);
}
static const struct wl_data_offer_listener offer_l = { .offer = offer_mime };

static void dd_offer(void *d, struct wl_data_device *dv, struct wl_data_offer *o)
{
	(void)d; (void)dv;
	wl_data_offer_add_listener(o, &offer_l, NULL);
}
/* no drag and drop */
static void dd_enter(void *d, struct wl_data_device *dv, uint32_t s, struct wl_surface *f, wl_fixed_t x, wl_fixed_t y, struct wl_data_offer *o)
{ (void)d; (void)dv; (void)s; (void)f; (void)x; (void)y; if (o) wl_data_offer_destroy(o); }
static void dd_leave(void *d, struct wl_data_device *dv) { (void)d; (void)dv; }
static void dd_motion(void *d, struct wl_data_device *dv, uint32_t t, wl_fixed_t x, wl_fixed_t y) { (void)d; (void)dv; (void)t; (void)x; (void)y; }
static void dd_drop(void *d, struct wl_data_device *dv) { (void)d; (void)dv; }
static void dd_selection(void *d, struct wl_data_device *dv, struct wl_data_offer *o)
{
	(void)d; (void)dv;
	if (offer) wl_data_offer_destroy(offer);
	offer = o;
}
static const struct wl_data_device_listener dd_l = { dd_offer, dd_enter, dd_leave, dd_motion, dd_drop, dd_selection };

/* A reader that is slow or never reads must not stop the editor, so a
   grandchild writes (as wl-copy, Neovim's provider, serves from its own
   process); waiting for the child alone leaves no zombie. */
static void src_send(void *d, struct wl_data_source *s, const char *m, int fd)
{
	(void)d; (void)s; (void)m;
	pid_t p = fork();
	if (p == 0) {
		if (fork() == 0)
			for (size_t i = 0; i < clipn;) {
				ssize_t w = write(fd, clip + i, clipn - i);
				if (w <= 0 && errno != EINTR) break;
				if (w > 0) i += (size_t)w;
			}
		_exit(0);
	}
	if (p > 0) waitpid(p, NULL, 0);
	close(fd);
}
static void src_target(void *d, struct wl_data_source *s, const char *m) { (void)d; (void)s; (void)m; }
static void src_cancelled(void *d, struct wl_data_source *s)
{
	(void)d;
	wl_data_source_destroy(s);
	if (s == src) src = NULL;
}
static const struct wl_data_source_listener src_l = { .target = src_target, .send = src_send, .cancelled = src_cancelled };

static int wl_clip_set(const char *s, size_t n)
{
	if (!ddev) return -1;
	char *c = malloc(n ? n : 1);
	if (!c) return -1;
	memcpy(c, s, n);
	free(clip);
	clip = c;
	clipn = n;
	if (src) wl_data_source_destroy(src);
	src = wl_data_device_manager_create_data_source(ddm);
	wl_data_source_add_listener(src, &src_l, NULL);
	for (int i = 0; i < 3; i++) wl_data_source_offer(src, MIME[i]);
	wl_data_device_set_selection(ddev, src, serial);
	wl_display_flush(dpy);
	return 0;
}

static long wl_clip_get(char *b, size_t max)
{
	if (src) {
		memcpy(b, clip, clipn < max ? clipn : max);
		return (long)clipn;
	}
	intptr_t m = offer ? (intptr_t)wl_data_offer_get_user_data(offer) : 0;
	int p[2];
	if (!m || pipe2(p, O_CLOEXEC)) return -1;
	wl_data_offer_receive(offer, MIME[m - 1], p[1]);
	close(p[1]);
	wl_display_flush(dpy);
	size_t n = 0;
	char sink[4096];
	struct pollfd pf = { p[0], POLLIN, 0 };
	for (long long end = now() + CLIP_MS * 1000000LL; now() < end;) {
		if (poll(&pf, 1, (int)((end - now()) / 1000000) + 1) <= 0) continue;
		ssize_t r = n < max ? read(p[0], b + n, max - n) : read(p[0], sink, sizeof sink);
		if (r == 0) { close(p[0]); return (long)n; }
		if (r > 0) n += (size_t)r;
		else if (errno != EINTR) break;
	}
	close(p[0]);
	return -1;
}

/* ---- globals ---- */

static void reg_add(void *d, struct wl_registry *r, uint32_t id, const char *i, uint32_t v)
{
	(void)d;
	if (!strcmp(i, wl_compositor_interface.name) && v >= 4)
		comp = wl_registry_bind(r, id, &wl_compositor_interface, 4);
	else if (!strcmp(i, wl_shm_interface.name))
		shm = wl_registry_bind(r, id, &wl_shm_interface, 1);
	else if (!strcmp(i, xdg_wm_base_interface.name)) {
		wm = wl_registry_bind(r, id, &xdg_wm_base_interface, 1);
		xdg_wm_base_add_listener(wm, &wm_l, NULL);
	} else if (!strcmp(i, wl_seat_interface.name) && v >= 4 && !seat) {
		seat = wl_registry_bind(r, id, &wl_seat_interface, 4);
		wl_seat_add_listener(seat, &seat_l, NULL);
	} else if (!strcmp(i, wl_output_interface.name) && v >= 2) {
		for (intptr_t k = 0; k < NOUT; k++)
			if (!out[k].o) {
				out[k].o = wl_registry_bind(r, id, &wl_output_interface, 2);
				out[k].scale = 1;
				wl_output_add_listener(out[k].o, &out_l, (void *)k);
				break;
			}
	} else if (!strcmp(i, wl_data_device_manager_interface.name))
		ddm = wl_registry_bind(r, id, &wl_data_device_manager_interface, 1);
	else if (!strcmp(i, zwp_text_input_manager_v1_interface.name))
		tim = wl_registry_bind(r, id, &zwp_text_input_manager_v1_interface, 1);
	else if (!strcmp(i, wp_cursor_shape_manager_v1_interface.name))
		csm = wl_registry_bind(r, id, &wp_cursor_shape_manager_v1_interface, 1);
}
static void reg_rm(void *d, struct wl_registry *r, uint32_t id) { (void)d; (void)r; (void)id; }
static const struct wl_registry_listener reg_l = { reg_add, reg_rm };

static int wl_open(void)
{
	xctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
	if (!xctx || tfd < 0 || !(dpy = wl_display_connect(NULL))) return -1;
	wl_registry_add_listener(wl_display_get_registry(dpy), &reg_l, NULL);
	if (wl_display_roundtrip(dpy) < 0 || !comp || !shm || !wm) return -1;
	rescale();
	if (!cw) return -1;
	kn = 0;   /* rescale's KEY_RESIZE: nothing is sized yet */
	surf = wl_compositor_create_surface(comp);
	wl_surface_add_listener(surf, &surf_l, NULL);
	xsurf = xdg_wm_base_get_xdg_surface(wm, surf);
	xdg_surface_add_listener(xsurf, &xs_l, NULL);
	top = xdg_surface_get_toplevel(xsurf);
	xdg_toplevel_add_listener(top, &top_l, NULL);
	xdg_toplevel_set_title(top, "homeros");
	xdg_toplevel_set_app_id(top, "homeros");
	if (ddm && seat) {
		ddev = wl_data_device_manager_get_data_device(ddm, seat);
		wl_data_device_add_listener(ddev, &dd_l, NULL);
	}
	if (tim) {
		ti = zwp_text_input_manager_v1_create_text_input(tim);
		zwp_text_input_v1_add_listener(ti, &ti_l, NULL);
	}
	wl_surface_commit(surf);
	while (!configured)
		if (wl_display_dispatch(dpy) < 0) return -1;
	return 0;
}

static void wl_close(void)
{
	if (dpy) wl_display_disconnect(dpy);
	dpy = NULL;
	font_close();
}

static int wl_fds(int fds[2])
{
	fds[0] = wl_display_get_fd(dpy);
	fds[1] = tfd;
	return 2;
}

static void wl_size(int *rows, int *cols)
{
	*rows = lh * scale / chh;
	*cols = lw * scale / cw;
	if (*rows < 1) *rows = 1;
	if (*cols < 1) *cols = 1;
}

static int wl_ready(void)
{
	return configured && !framecb && (!buf[0].busy || !buf[1].busy);
}

static void wl_mouse(int *r, int *c) { *r = mrow; *c = mcol; }

static void wl_suspend(void) {}   /* a window has no terminal to give back */

/* ---- drawing ---- */

/* Catppuccin Mocha (FEATURES.md), per face the colour ref/tty.c gives it
   (catppuccin/nvim's treesitter.lua).  One font face, so its italic, bold,
   underline and strikethrough are not drawn. */
enum { TEXT = 0xcdd6f4 };
static const uint32_t FACE[] = {
	TEXT,
	0xcdd6f4, /* variable: text */
	0xf38ba8, /* variable.builtin: red */
	0xeba0ac, /* variable.parameter: maroon */
	0xeba0ac, /* variable.parameter.builtin: maroon */
	0xb4befe, /* variable.member: lavender */
	0xfab387, /* constant: peach */
	0xfab387, /* constant.builtin: peach */
	0xcba6f7, /* constant.macro: mauve */
	0xf9e2af, /* module: yellow, italic */
	0xf9e2af, /* module.builtin: yellow, italic */
	0x74c7ec, /* label: sapphire */
	0xa6e3a1, /* string: green */
	0x94e2d5, /* string.documentation: teal */
	0xf5c2e7, /* string.regexp: pink */
	0xf5c2e7, /* string.escape: pink */
	0xf5c2e7, /* string.special: pink */
	0xf2cdcd, /* string.special.symbol: flamingo */
	0xf5c2e7, /* string.special.path: pink */
	0x89b4fa, /* string.special.url: blue, italic underline */
	0x94e2d5, /* character: teal */
	0xf5c2e7, /* character.special: pink */
	0xfab387, /* boolean: peach */
	0xfab387, /* number: peach */
	0xfab387, /* number.float: peach */
	0xf9e2af, /* type: yellow */
	0xcba6f7, /* type.builtin: mauve */
	0xf9e2af, /* type.definition: yellow */
	0xfab387, /* attribute: peach */
	0xfab387, /* attribute.builtin: peach */
	0xb4befe, /* property: lavender */
	0x89b4fa, /* function: blue */
	0xfab387, /* function.builtin: peach */
	0x89b4fa, /* function.call: blue */
	0xf5c2e7, /* function.macro: pink */
	0x89b4fa, /* function.method: blue */
	0x89b4fa, /* function.method.call: blue */
	0xf9e2af, /* constructor: yellow */
	0x89dceb, /* operator: sky */
	0xcba6f7, /* keyword: mauve */
	0xcba6f7, /* keyword.coroutine: mauve */
	0xcba6f7, /* keyword.function: mauve */
	0xcba6f7, /* keyword.operator: mauve */
	0xcba6f7, /* keyword.import: mauve */
	0xcba6f7, /* keyword.type: mauve */
	0xcba6f7, /* keyword.modifier: mauve */
	0xcba6f7, /* keyword.repeat: mauve */
	0xcba6f7, /* keyword.return: mauve */
	0xcba6f7, /* keyword.debug: mauve */
	0xcba6f7, /* keyword.exception: mauve */
	0xcba6f7, /* keyword.conditional: mauve, italic */
	0x89dceb, /* keyword.conditional.ternary: sky */
	0xf5c2e7, /* keyword.directive: pink */
	0xf5c2e7, /* keyword.directive.define: pink */
	0x9399b2, /* punctuation.delimiter: overlay2 */
	0x9399b2, /* punctuation.bracket: overlay2 */
	0xf5c2e7, /* punctuation.special: pink */
	0x9399b2, /* comment: overlay2, italic */
	0x9399b2, /* comment.documentation: overlay2, italic */
	0x1e1e2e, /* comment.error: base on red */
	0x1e1e2e, /* comment.warning: base on yellow */
	0x1e1e2e, /* comment.todo: base on flamingo */
	0x1e1e2e, /* comment.note: base on rosewater */
	0xf38ba8, /* markup.strong: red, bold */
	0xf38ba8, /* markup.italic: red, italic */
	0xcdd6f4, /* markup.strikethrough: text, strikethrough */
	TEXT, /* markup.underline: underline */
	0x89b4fa, /* markup.heading: blue */
	0x89b4fa, /* markup.heading.1: blue */
	0x89b4fa, /* markup.heading.2: blue */
	0x89b4fa, /* markup.heading.3: blue */
	0x89b4fa, /* markup.heading.4: blue */
	0x89b4fa, /* markup.heading.5: blue */
	0x89b4fa, /* markup.heading.6: blue */
	0xf5c2e7, /* markup.quote: pink */
	0x89b4fa, /* markup.math: blue */
	0xb4befe, /* markup.link: lavender */
	0xb4befe, /* markup.link.label: lavender */
	0x89b4fa, /* markup.link.url: blue, italic underline */
	0xa6e3a1, /* markup.raw: green */
	0xa6e3a1, /* markup.raw.block: green */
	0x94e2d5, /* markup.list: teal */
	0xa6e3a1, /* markup.list.checked: green */
	0x7f849c, /* markup.list.unchecked: overlay1 */
	0xa6e3a1, /* diff.plus: green */
	0xf38ba8, /* diff.minus: red */
	0x89b4fa, /* diff.delta: blue */
	0x89b4fa, /* tag: blue */
	0x89b4fa, /* tag.builtin: blue */
	0xf9e2af, /* tag.attribute: yellow, italic */
	0x94e2d5, /* tag.delimiter: teal */
};
_Static_assert(sizeof FACE / sizeof *FACE == FACE_COUNT, "a colour per face");
/* comment.error, .warning, .todo, .note: base on a colour */
static const uint32_t NOTE_BG[] = { 0xf38ba8, 0xf9e2af, 0xf2cdcd, 0xf5e0dc };
enum { BG = 0x1e1e2e, DIM = 0x6c7086, CURSOR = 0xf5e0dc, PREBG = 0x45475a };

static uint32_t fg_of(const Cell *c)
{
	if (c->attr & A_CUR || (c->attr & 3) == ATTR_REV) return BG;
	if ((c->attr & 3) == ATTR_DIM) return DIM;
	return c->face < FACE_COUNT ? FACE[c->face] : TEXT;
}

static uint32_t bg_of(const Cell *c)
{
	return c->attr & A_CUR ? CURSOR : (c->attr & 3) == ATTR_REV ? TEXT : c->attr & A_PRE ? PREBG
	     : c->face >= FACE_COMMENT_ERROR && c->face <= FACE_COMMENT_NOTE ? NOTE_BG[c->face - FACE_COMMENT_ERROR] : BG;
}

/* Pixel loops keep px and the width in locals: a store through px might
   alias b, so the compiler would reload both per pixel (measured: fill was
   68% of a whole-window frame). */
static void fill(Buf *b, int x, int y, int w, int h, uint32_t c)
{
	int W = b->w;
	if (x + w > W) w = W - x;
	if (y + h > b->h) h = b->h - y;
	if (w <= 0 || h <= 0) return;
	uint32_t *p = b->px + (size_t)y * (size_t)W + (size_t)x;
	for (int i = 0; i < w; i++) p[i] = c;
	for (int j = 1; j < h; j++) memcpy(p + (size_t)j * (size_t)W, p, (size_t)w * 4);   /* libc's copy is vectorized */
}

/* d + (s - d) * a / 255 per channel, red and blue in one multiply: each
   product fits its 16 bits, and (x + 128 + ((x + 128) >> 8)) >> 8 is x / 255
   rounded for x up to 255 * 255 (Blinn, "Three wrongs make a right") */
static uint32_t mix(uint32_t d, uint32_t s, uint32_t a)
{
	uint32_t rb = (d & 0xff00ff) * (255 - a) + (s & 0xff00ff) * a + 0x800080;
	uint32_t g = (d >> 8 & 0xff) * (255 - a) + (s >> 8 & 0xff) * a + 0x80;
	rb = (rb + (rb >> 8 & 0xff00ff)) >> 8 & 0xff00ff;
	g = (g + (g >> 8)) >> 8;
	return rb | g << 8;
}

/* A glyph clipped to its row's band, so a frame never paints a row it did
   not redraw. */
static void blit(Buf *b, const Glyph *g, int x0, int y0, int top, int bot, uint32_t fg)
{
	uint32_t *px = b->px;
	int W = b->w, H = b->h;
	for (int j = 0; j < g->h; j++) {
		int y = y0 + j;
		if (y < top || y >= bot || y >= H) continue;
		for (int i = 0; i < g->w; i++) {
			int x = x0 + i;
			if (x < 0 || x >= W) continue;
			uint32_t *p = &px[y * W + x];
			if (g->color) {   /* premultiplied BGRA: over */
				const unsigned char *s = g->px + (j * g->w + i) * 4;
				uint32_t r = 0;
				for (int k = 0; k < 3; k++)
					r |= (uint32_t)(s[k] + ((*p >> (8 * k) & 255) * (255u - s[3]) + 127) / 255) << (8 * k);
				*p = r;
			} else if (g->px[j * g->w + i]) *p = mix(*p, fg, g->px[j * g->w + i]);
		}
	}
}

/* Row r with the cursor and the preedit drawn into its cells, so comparing
   rows also catches a moved cursor or a changed preedit. */
static const Cell *compose(const Grid *g, int r)
{
	memcpy(row, &g->c[r * g->cols], (size_t)g->cols * sizeof *row);
	if (r != g->crow || g->ccol >= g->cols) return row;
	int c = g->ccol;
	size_t n = strlen(pre);
	for (size_t i = 0; i < n; i += uni_len((const unsigned char *)pre, n, i)) {
		size_t l = uni_len((const unsigned char *)pre, n, i);
		int w = uni_width(uni_cp((const unsigned char *)pre, n, i));
		if (c + w > g->cols) break;
		row[c] = (Cell){ { 0 }, (unsigned char)l, A_PRE, FACE_NONE };
		memcpy(row[c].ch, pre + i, l);
		if (w == 2) row[c + 1] = (Cell){ "", 0, A_PRE, FACE_NONE };
		c += w;
	}
	if (c < g->cols) row[c].attr |= A_CUR;
	return row;
}

/* Bytes, not grid_cell_eq: a cell's bytes past its length may differ where
   the cells do not, which costs a row's redraw, never a wrong one. */
static int same(const Grid *a, const Cell *x, int r)
{
	return !memcmp(&a->c[r * a->cols], x, (size_t)a->cols * sizeof *x);
}

/* Draws one row: backgrounds, then each run of cells with one attr and face
   shaped as a whole (ligatures stay inside a run). */
static void draw_row(Buf *b, const Cell *x, int cols, int r)
{
	int y0 = r * chh;
	for (int c = 0, e; c < cols; c = e) {
		uint32_t bg = bg_of(&x[c]);
		for (e = c + 1; e < cols && bg_of(&x[e]) == bg; e++) {}
		fill(b, c * cw, y0, (e - c) * cw, chh, bg);
	}
	for (int c = 0; c < cols;) {
		int e = c;
		size_t n = 0;
		for (; e < cols && (e == c || (x[e].attr == x[c].attr && x[e].face == x[c].face) || !x[e].len); e++) {
			memcpy(run + n, x[e].ch, x[e].len);
			for (int k = 0; k < x[e].len; k++) col_of[n + (size_t)k] = e;
			n += x[e].len;
		}
		int np = n ? font_shape(run, n, placed, cap * 4) : 0;
		for (int i = 0; i < np; i++) {
			const Placed *p = &placed[i];
			blit(b, p->g, col_of[p->cluster] * cw + p->x + p->g->left, y0 + base - p->y - p->g->top,
			     y0, y0 + chh, fg_of(&x[c]));
		}
		c = e;
	}
}

static void buf_release(void *d, struct wl_buffer *w) { (void)w; ((Buf *)d)->busy = 0; }
static const struct wl_buffer_listener buf_l = { buf_release };

/* Resize path, not the redraw path: allocation is allowed here. */
static int buf_fit(Buf *b, int w, int h)
{
	if (b->w == w && b->h == h && b->wb) return 0;
	if (b->wb) { wl_buffer_destroy(b->wb); munmap(b->px, b->size); b->wb = NULL; }
	b->valid = 0;
	b->size = (size_t)w * (size_t)h * 4;
	int fd = memfd_create("homeros", MFD_CLOEXEC);
	if (fd < 0 || ftruncate(fd, (off_t)b->size)) { if (fd >= 0) close(fd); return -1; }
	b->px = mmap(NULL, b->size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (b->px == MAP_FAILED) { close(fd); return -1; }
	struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, (int32_t)b->size);
	b->wb = wl_shm_pool_create_buffer(pool, 0, w, h, w * 4, WL_SHM_FORMAT_XRGB8888);
	wl_shm_pool_destroy(pool);
	close(fd);
	wl_buffer_add_listener(b->wb, &buf_l, b);
	b->w = w;
	b->h = h;
	return 0;
}

static int scratch(int cols)
{
	if (cols <= cap) return 0;
	Cell *r = realloc(row, (size_t)cols * sizeof *r);
	if (r) row = r;
	unsigned char *u = realloc(run, (size_t)cols * 4);
	if (u) run = u;
	int *m = realloc(col_of, (size_t)cols * 4 * sizeof *m);
	if (m) col_of = m;
	Placed *p = realloc(placed, (size_t)cols * 4 * sizeof *p);
	if (p) placed = p;
	if (!r || !u || !m || !p) return -1;
	cap = cols;
	return 0;
}

static void frame_done(void *d, struct wl_callback *cb, uint32_t t)
{
	(void)d; (void)t;
	wl_callback_destroy(cb);
	framecb = 0;
}
static const struct wl_callback_listener frame_l = { frame_done };

/* Draws g into a free buffer: the rows that buffer lacks; damage is the rows
   that differ from the frame on screen. */
static void paint(const Grid *g)
{
	Buf *b = !last->busy ? last : &buf[last == buf];
	Buf *on = last;
	if (b->busy || buf_fit(b, lw * scale, lh * scale) || scratch(g->cols)) return;
	if (b->g.rows != g->rows || b->g.cols != g->cols) {
		if (grid_resize(&b->g, g->rows, g->cols)) return;
		b->valid = 0;
	}
	int whole = !b->valid || !on->valid || on->g.rows != g->rows || on->g.cols != g->cols || on->w != b->w || on->h != b->h;
	if (!b->valid) {   /* the margins no cell covers */
		fill(b, g->cols * cw, 0, b->w, b->h, BG);
		fill(b, 0, g->rows * chh, b->w, b->h, BG);
	}
	for (int r = 0; r < g->rows && r * chh < b->h; r++) {
		const Cell *x = compose(g, r);
		int show = whole || !same(&on->g, x, r);
		if (!b->valid || !same(&b->g, x, r)) {
			draw_row(b, x, g->cols, r);
			memcpy(&b->g.c[r * g->cols], x, (size_t)g->cols * sizeof *x);
		}
		if (show && !whole) wl_surface_damage_buffer(surf, 0, r * chh, b->w, chh);
	}
	if (whole) wl_surface_damage_buffer(surf, 0, 0, b->w, b->h);
	b->valid = 1;
	b->g.crow = g->crow;
	b->g.ccol = g->ccol;
	wl_surface_attach(surf, b->wb, 0, 0);
	wl_callback_add_listener(wl_surface_frame(surf), &frame_l, NULL);
	wl_surface_commit(surf);
	b->busy = 1;
	last = b;
	framecb = 1;
	pre_dirty = 0;
	if (ti && (ti_serial == 0 || on->g.crow != g->crow || on->g.ccol != g->ccol)) {
		zwp_text_input_v1_set_cursor_rectangle(ti, g->ccol * cw / scale, g->crow * chh / scale, cw / scale, chh / scale);
		zwp_text_input_v1_commit_state(ti, ++ti_serial);
	}
	wl_display_flush(dpy);
}

static void wl_present(const Grid *g, Grid *shown)
{
	if (cur.rows != g->rows || cur.cols != g->cols) {
		if (grid_resize(&cur, g->rows, g->cols)) return;
	}
	if (shown->rows != g->rows || shown->cols != g->cols) {
		if (grid_resize(shown, g->rows, g->cols)) return;
		buf[0].valid = buf[1].valid = 0;   /* the contract: redrawn whole */
	}
	memcpy(cur.c, g->c, (size_t)g->rows * (size_t)g->cols * sizeof *g->c);
	cur.crow = g->crow;
	cur.ccol = g->ccol;
	memcpy(shown->c, g->c, (size_t)g->rows * (size_t)g->cols * sizeof *g->c);
	shown->crow = g->crow;
	shown->ccol = g->ccol;
	paint(&cur);
}

static int wl_input(int *k, int n)
{
	uint64_t x;
	if (read(tfd, &x, sizeof x) < 0) {}   /* only clears the timer */
	/* events are read only into an empty queue, so one read's keys fit */
	if (!kn && !gone) {
		struct pollfd p = { wl_display_get_fd(dpy), POLLIN, 0 };
		int e = 0;
		while (!e && wl_display_prepare_read(dpy)) e = wl_display_dispatch_pending(dpy) < 0;
		if (!e) e = poll(&p, 1, 0) > 0 ? wl_display_read_events(dpy) < 0 : (wl_display_cancel_read(dpy), 0);
		if (e || wl_display_dispatch_pending(dpy) < 0) {
			/* the compositor is gone: quit without writing, as Neovim does
			   when it loses its terminal (SIGHUP) */
			gone = 1;
			push_utf8("\x1b:qa!\r");
		}
	}
	long long t = now();
	if (rep_key && t >= rep_next) {
		long long c = 1 + (t - rep_next) / rep_ns;
		for (long long i = 0; i < c; i++) push(rep_key);
		rep_next += c * rep_ns;
	}
	int m = 0;
	for (; m < n && kn; m++, kn--, kh = (kh + 1) % KQ) k[m] = kq[kh];
	arm();
	if (pre_dirty && cur.rows && wl_ready()) paint(&cur);
	wl_display_flush(dpy);
	return m;
}

const Backend backend_wl = {
	wl_open, wl_close, wl_fds, wl_input, wl_size, wl_ready,
	wl_present, wl_suspend, wl_clip_set, wl_clip_get, wl_mouse
};
