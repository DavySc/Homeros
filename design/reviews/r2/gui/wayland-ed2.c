/* Minimal GUI text grid on raw Wayland: wl_shm buffers, FreeType glyphs,
   xkbcommon keys, one poll() loop, redraw only on events.  INJECT=1 types
   200 'x' at 10 ms from a timerfd.  Timings to stderr. */
#define _GNU_SOURCE
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include "xdg-shell.h"
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1e3+t.tv_nsec/1e6;}
enum { W = 1600, H = 1000, ROWS = 60 };
static struct wl_compositor *comp; static struct wl_shm *shm; static struct xdg_wm_base *wm; static struct wl_seat *seat;
static struct wl_surface *surf; static struct xkb_context *xctx; static struct xkb_state *xst;
static struct buf { struct wl_buffer *b; uint32_t *px; int busy; } bufs[2];
static char line[ROWS][256]; static int dirty = 1, configured, quit, cw, ch, asc;
static FT_Face face; static struct { unsigned char *bm; int w, h, l, t, adv; } gl[128];
static double t0, tdraw; static int frames;
/* review additions: key-to-commit latency, optional frame-callback throttle, first-frame-shown time */
static double keyt[100000], lat[100000]; static int nkey, nshown, usecb, waitcb, firstcb; static struct wl_display *gdpy;
static void cb_done(void *d, struct wl_callback *c, uint32_t t) { (void)d; (void)t; wl_callback_destroy(c); waitcb = 0; if (!firstcb) { firstcb = 1; fprintf(stderr, "first frame callback (compositor repainted) %.1f ms after start\n", now() - t0); } }
static const struct wl_callback_listener cb_l = { cb_done };
static int cmpd(const void *a, const void *b) { double x = *(double *)a, y = *(double *)b; return x < y ? -1 : x > y; }

static void glyphs(const char *font) {
  FT_Library ft; FT_Init_FreeType(&ft); FT_New_Face(ft, font, 0, &face); FT_Set_Pixel_Sizes(face, 0, 20);
  for (int c = 32; c < 127; c++) { FT_Load_Char(face, c, FT_LOAD_RENDER); FT_Bitmap *b = &face->glyph->bitmap;
    gl[c].w = b->width; gl[c].h = b->rows; gl[c].l = face->glyph->bitmap_left; gl[c].t = face->glyph->bitmap_top; gl[c].adv = face->glyph->advance.x >> 6;
    gl[c].bm = malloc(b->width * b->rows + 1); for (unsigned y = 0; y < b->rows; y++) memcpy(gl[c].bm + y * b->width, b->buffer + y * b->pitch, b->width); }
  cw = gl['M'].adv; ch = face->size->metrics.height >> 6; asc = face->size->metrics.ascender >> 6;
}
static void draw_line(uint32_t *px, int row) {
  int y0 = 4 + row * ch; for (int y = y0; y < y0 + ch && y < H; y++) for (int x = 0; x < W; x++) px[y * W + x] = 0xff1e1e1e;
  int x = 4; for (const unsigned char *s = (unsigned char *)line[row]; *s && x < W - cw; s++, x += cw) {
    int c = *s == '\t' ? ' ' : *s < 127 && *s >= 32 ? *s : '?';
    for (int gy = 0; gy < gl[c].h; gy++) { int py = y0 + asc - gl[c].t + gy; if (py < 0 || py >= H) continue;
      for (int gx = 0; gx < gl[c].w; gx++) { unsigned a = gl[c].bm[gy * gl[c].w + gx]; if (!a) continue;
        unsigned v = 0x1e + ((0xd4 - 0x1e) * a) / 255; px[py * W + x + gl[c].l + gx] = 0xff000000 | v << 16 | v << 8 | v; } } }
}
static void buf_release(void *d, struct wl_buffer *b) { (void)b; ((struct buf *)d)->busy = 0; }
static const struct wl_buffer_listener buf_l = { buf_release };
static void make_bufs(void) {
  int fd = memfd_create("ed", 0); size_t sz = (size_t)W * H * 4; ftruncate(fd, 2 * sz);
  uint32_t *m = mmap(0, 2 * sz, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0); struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, 2 * sz);
  for (int i = 0; i < 2; i++) { bufs[i].px = m + i * W * H; bufs[i].b = wl_shm_pool_create_buffer(pool, i * sz, W, H, W * 4, WL_SHM_FORMAT_XRGB8888); wl_buffer_add_listener(bufs[i].b, &buf_l, &bufs[i]); }
  wl_shm_pool_destroy(pool); close(fd);
}
static void redraw(void) {
  if (usecb && waitcb) return;
  struct buf *b = !bufs[0].busy ? &bufs[0] : !bufs[1].busy ? &bufs[1] : 0; if (!b) return; /* both held: draw at next release */
  double a = now();
  for (int i = 0; i < ROWS; i++) draw_line(b->px, i);
  if (usecb || !frames) { wl_callback_add_listener(wl_surface_frame(surf), &cb_l, 0); waitcb = 1; }
  wl_surface_attach(surf, b->b, 0, 0); wl_surface_damage_buffer(surf, 0, 0, W, H); wl_surface_commit(surf); b->busy = 1; dirty = 0;
  double e = now(); while (nshown < nkey) { lat[nshown] = e - keyt[nshown]; nshown++; }
  if (!frames) fprintf(stderr, "first frame %.1f ms after start\n", e - t0); else tdraw += e - a; frames++;
}
static void wm_ping(void *d, struct xdg_wm_base *w, uint32_t s) { (void)d; xdg_wm_base_pong(w, s); }
static const struct xdg_wm_base_listener wm_l = { wm_ping };
static void xs_conf(void *d, struct xdg_surface *x, uint32_t s) { (void)d; xdg_surface_ack_configure(x, s); configured = 1; dirty = 1; }
static const struct xdg_surface_listener xs_l = { xs_conf };
static void top_conf(void *d, struct xdg_toplevel *t, int32_t w, int32_t h, struct wl_array *s) { (void)d; (void)t; (void)w; (void)h; (void)s; }
static void top_close(void *d, struct xdg_toplevel *t) { (void)d; (void)t; quit = 1; }
static const struct xdg_toplevel_listener top_l = { top_conf, top_close };
static void type(const char *s) { size_t n = strlen(line[0]); if (n + strlen(s) >= 255) { line[0][0] = 0; } strcat(line[0], s); dirty = 1; if (nkey < 100000) keyt[nkey++] = now(); }
static void kb_map(void *d, struct wl_keyboard *k, uint32_t f, int fd, uint32_t sz) { (void)d; (void)k; (void)f;
  char *m = mmap(0, sz, PROT_READ, MAP_PRIVATE, fd, 0); struct xkb_keymap *km = xkb_keymap_new_from_string(xctx, m, XKB_KEYMAP_FORMAT_TEXT_V1, 0);
  munmap(m, sz); close(fd); xst = xkb_state_new(km); }
static void kb_enter(void *d, struct wl_keyboard *k, uint32_t s, struct wl_surface *f, struct wl_array *a) { (void)d; (void)k; (void)s; (void)f; (void)a; }
static void kb_leave(void *d, struct wl_keyboard *k, uint32_t s, struct wl_surface *f) { (void)d; (void)k; (void)s; (void)f; }
static void kb_key(void *d, struct wl_keyboard *k, uint32_t s, uint32_t t, uint32_t key, uint32_t st) { (void)d; (void)k; (void)s; (void)t;
  if (st != WL_KEYBOARD_KEY_STATE_PRESSED || !xst) return; char u[8]; if (xkb_state_key_get_utf8(xst, key + 8, u, sizeof u) > 0) type(u); }
static void kb_mods(void *d, struct wl_keyboard *k, uint32_t s, uint32_t a, uint32_t b, uint32_t c, uint32_t g) { (void)d; (void)k; (void)s; if (xst) xkb_state_update_mask(xst, a, b, c, 0, 0, g); }
static void kb_rep(void *d, struct wl_keyboard *k, int32_t r, int32_t dl) { (void)d; (void)k; (void)r; (void)dl; }
static const struct wl_keyboard_listener kb_l = { kb_map, kb_enter, kb_leave, kb_key, kb_mods, kb_rep };
static void seat_caps(void *d, struct wl_seat *s, uint32_t c) { (void)d; if (c & WL_SEAT_CAPABILITY_KEYBOARD) wl_keyboard_add_listener(wl_seat_get_keyboard(s), &kb_l, 0); }
static void seat_name(void *d, struct wl_seat *s, const char *n) { (void)d; (void)s; (void)n; }
static const struct wl_seat_listener seat_l = { seat_caps, seat_name };
static void reg_add(void *d, struct wl_registry *r, uint32_t id, const char *i, uint32_t v) { (void)d; (void)v;
  if (!strcmp(i, "wl_compositor")) comp = wl_registry_bind(r, id, &wl_compositor_interface, 4);
  else if (!strcmp(i, "wl_shm")) shm = wl_registry_bind(r, id, &wl_shm_interface, 1);
  else if (!strcmp(i, "xdg_wm_base")) { wm = wl_registry_bind(r, id, &xdg_wm_base_interface, 1); xdg_wm_base_add_listener(wm, &wm_l, 0); }
  else if (!strcmp(i, "wl_seat")) { seat = wl_registry_bind(r, id, &wl_seat_interface, 5); wl_seat_add_listener(seat, &seat_l, 0); } }
static void reg_rm(void *d, struct wl_registry *r, uint32_t id) { (void)d; (void)r; (void)id; }
static const struct wl_registry_listener reg_l = { reg_add, reg_rm };

int main(int argc, char **argv) {
  t0 = now();
  FILE *f = fopen(argv[1], "r"); for (int i = 0; f && i < ROWS && fgets(line[i], 256, f); i++) line[i][strcspn(line[i], "\n")] = 0; if (f) fclose(f);
  glyphs(argv[2]); xctx = xkb_context_new(0);
  struct wl_display *dpy = wl_display_connect(0); if (!dpy) { fprintf(stderr, "no wayland\n"); return 1; }
  wl_registry_add_listener(wl_display_get_registry(dpy), &reg_l, 0); wl_display_roundtrip(dpy);
  surf = wl_compositor_create_surface(comp); struct xdg_surface *xs = xdg_wm_base_get_xdg_surface(wm, surf); xdg_surface_add_listener(xs, &xs_l, 0);
  struct xdg_toplevel *top = xdg_surface_get_toplevel(xs); xdg_toplevel_add_listener(top, &top_l, 0); xdg_toplevel_set_title(top, "ed");
  wl_surface_commit(surf); make_bufs();
  int tfd = -1, injected = 0, nk = getenv("NKEYS") ? atoi(getenv("NKEYS")) : 200; usecb = getenv("FRAMECB") != 0; gdpy = dpy;
  if (getenv("INJECT")) { tfd = timerfd_create(CLOCK_MONOTONIC, 0); long us = getenv("RATE_US") ? atol(getenv("RATE_US")) : 10000; struct itimerspec it = { {us / 1000000, us % 1000000 * 1000}, {0, 500000000} }; timerfd_settime(tfd, 0, &it, 0); }
  struct pollfd p[2] = { { wl_display_get_fd(dpy), POLLIN, 0 }, { tfd, POLLIN, 0 } };
  while (!quit) {
    if (configured && dirty) redraw();
    while (wl_display_prepare_read(dpy)) wl_display_dispatch_pending(dpy);
    wl_display_flush(dpy);
    if (poll(p, tfd < 0 ? 1 : 2, -1) < 0) { wl_display_cancel_read(dpy); break; }
    if (p[0].revents & POLLIN) wl_display_read_events(dpy); else wl_display_cancel_read(dpy);
    wl_display_dispatch_pending(dpy);
    if (tfd >= 0 && p[1].revents & POLLIN) { uint64_t n; read(tfd, &n, 8); if (injected++ < nk) type("x"); else if (!dirty) quit = 1; }
  }
  if (frames > 1) fprintf(stderr, "redraws %d, mean %.2f ms\n", frames - 1, tdraw / (frames - 1));
  if (nshown) { qsort(lat, nshown, sizeof *lat, cmpd); fprintf(stderr, "keys %d: key-to-commit p50 %.2f  p99 %.2f  max %.2f ms\n", nshown, lat[nshown / 2], lat[nshown * 99 / 100], lat[nshown - 1]); }
  return 0;
}
