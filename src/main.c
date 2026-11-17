/* The editor's main loop, Render.tla "ok": draw only between commands, at
   most BATCH commands per frame, and only when the backend says a frame may
   be drawn (the compositor's frame callback, Present.tla). */
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "backend.h"
#include "core.h"
#include "loop.h"
#include "syntax.h"

enum { BATCH = 64, QMAX = 4096 };

static const Backend *b = &backend_tty;
static int q[QMAX], qh, qn;     /* keys read but not yet run */
static int resized;
static struct pollfd in[2];
static int nin;

static void on_input(void *ctx, int fd)
{
	(void)ctx; (void)fd;
	int k[256];
	/* Reads even when the queue is full, so a Ctrl-C behind a long paste
	   still stops a command (Break.tla "ok"); keys that do not fit are lost. */
	int n = b->input(k, 256);
	for (int i = 0; i < n; i++) {
		if (k[i] == KEY_RESIZE) { resized = 1; continue; }
		/* Neovim's got_int; the keys typed before it go (flush_buffers) */
		if (k[i] == KEY_CTRL_C) { loop_int = 1; qn = 0; }
		if (qn < QMAX) q[(qh + qn++) % QMAX] = k[i];
	}
}

/* syntax_update's stop: is input waiting? */
static int input_waiting(void) { return poll(in, (nfds_t)nin, 0) > 0; }

int main(int argc, char **argv)
{
	static Editor e;
	Grid g = { 0 }, shown = { 0 };
	int fds[2], rows, cols, tty = argc == 3 && !strcmp(argv[1], "-t");
	if (argc != 2 + tty) { fprintf(stderr, "usage: homeros [-t] FILE\n"); return 2; }
#if defined _WIN32
	if (!tty) b = &backend_win;
#elif defined HOMEROS_WL
	if (!tty && getenv("WAYLAND_DISPLAY")) b = &backend_wl;
#endif
	if (core_open(&e, argv[1 + tty])) { fprintf(stderr, "homeros: %s\n", e.msg); return 1; }
	if (b->open()) { fprintf(stderr, "homeros: no terminal or window\n"); core_free(&e); return 1; }
	nin = b->fds(fds);
	for (int i = 0; i < nin; i++) {
		in[i] = (struct pollfd){ .fd = fds[i], .events = POLLIN };
		loop_watch(fds[i], on_input, NULL);
	}
	b->size(&rows, &cols);
	if (grid_resize(&g, rows, cols)) goto out;
	int dirty = 1, syn = e.buf->syn != NULL;   /* syn: a reparse is pending */
	while (!e.quit) {
		if (resized) {
			resized = 0;
			b->size(&rows, &cols);
			if (grid_resize(&g, rows, cols)) break;
			dirty = 1;
		}
		for (int i = 0; i < BATCH && qn && !e.quit; i++, dirty = 1) {
			int k = q[qh];
			qh = (qh + 1) % QMAX;
			qn--;
			if (k == KEY_CTRL_Z && e.mode == MODE_NORMAL) {
				b->suspend();
				grid_free(&shown);     /* the terminal was used meanwhile: redraw all */
			} else keys_feed(&e, k, qn > 0);
			loop_int = 0;
		}
		if (e.quit) break;
		if (dirty) syn = e.buf->syn != NULL;
		if (dirty && b->ready()) {
			core_draw(&e, &g);
			b->present(&g, &shown);
			dirty = 0;
		}
		/* deferred work only once the frame is out and no key waits */
		if (syn && !dirty && !qn) {
			int r = syntax_update(e.buf->syn, e.buf->text, input_waiting);
			syn = r == 2;
			dirty = r == 1;
		}
		if (loop_once(qn || syn || (dirty && b->ready()) ? 0 : -1)) break;
	}
out:
	b->close();
	grid_free(&g);
	grid_free(&shown);
	core_free(&e);
	return 0;
}
