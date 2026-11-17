/* The editor's main loop, Render.tla "ok": draw only between commands, at
   most BATCH commands per frame, and only when the backend says a frame may
   be drawn (the compositor's frame callback, Present.tla). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "backend.h"
#include "core.h"
#include "git.h"
#include "loop.h"
#include "lsp.h"
#include "syntax.h"
#include "tools.h"

enum { BATCH = 64, QMAX = 4096 };

static const Backend *b = &backend_tty;
static int q[QMAX], qh, qn;     /* keys read but not yet run */
static int resized, running;   /* running: inside a command */

static void on_input(void *ctx, int fd)
{
	(void)ctx; (void)fd;
	int k[256];
	/* Between commands the loop drains the queue, so a full queue leaves the
	   input waiting in the terminal and nothing is lost.  Inside a command
	   (loop_breakcheck) it reads on, so a Ctrl-C behind a long paste still
	   stops the command; keys that do not fit then are dropped. */
	int room = QMAX - qn;
	if (!room && !running) return;
	int n = b->input(k, room && room < 256 ? room : 256);
	for (int i = 0; i < n; i++) {
		if (k[i] == KEY_RESIZE) { resized = 1; continue; }
		/* Neovim's got_int; the keys typed before it go (Vim's
		   fill_input_buf() drops what was typed before a CTRL-C) */
		if (k[i] == KEY_CTRL_C) { loop_int = 1; qn = 0; }
		if (qn < QMAX) q[(qh + qn++) % QMAX] = k[i];
	}
}

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
	for (int i = 0, n = b->fds(fds); i < n; i++) loop_watch(fds[i], on_input, NULL);
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
			} else {
				running = 1;
				if (e.ks.pending || (!tools_key(&e, k) && !git_key(&e, k))) keys_feed(&e, k, qn > 0);
				running = 0;
				lsp_flush(e.buf->lsp);   /* one didChange per command (Helix) */
			}
			loop_int = 0;
		}
		if (e.quit) break;
		if (dirty) syn = e.buf->syn != NULL;
		if (dirty && b->ready()) {
			core_draw(&e, &g);
			tools_draw(&e, &g);
			b->present(&g, &shown);
			dirty = 0;
		}
		/* deferred work only once the frame is out and no key waits */
		if (syn && !dirty && !qn) {
			int r = syntax_update(e.buf->syn, e.buf->text, loop_waiting);
			syn = r == 2;
			dirty = r == 1;
		}
		if (loop_once(qn || syn || (dirty && b->ready()) ? 0 : -1)) break;
		if (core_changed) { core_changed = 0; dirty = 1; }
	}
out:
	git_free();
	b->close();
	grid_free(&g);
	grid_free(&shown);
	core_free(&e);
	return 0;
}
