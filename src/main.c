/* The editor's main loop, Render.tla "ok": draw only between commands, at
   most BATCH commands per frame, and only when the backend says a frame may
   be drawn (the compositor's frame callback, Present.tla). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if defined _WIN32
#include <windows.h>
#endif
#include "backend.h"
#include "bridge.h"
#include "core.h"
#include "git.h"
#include "kitten.h"
#include "loop.h"
#include "lsp.h"
#include "shada.h"
#include "syntax.h"
#include "tools.h"

enum { BATCH = 64, QMAX = 4096 };

static const Backend *b = &backend_tty;
static int q[QMAX], qh, qn;     /* keys read but not yet run */
static int resized, running;   /* running: inside a command */
static int lost;               /* the backend's input is gone for good */

static void on_watch(void *ctx, int fd) { core_watch(ctx, fd); }

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
	if (n < 0) { lost = 1; loop_int = 1; return; }
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
	struct timespec t0;
	timespec_get(&t0, TIME_UTC);   /* for the dashboard's "loaded in" */
	int fds[2], rows, cols, tty = argc > 1 && !strcmp(argv[1], "-t");
#if defined _WIN32
	/* a GUI-subsystem program has no console: borrow the one it was started
	   from, if any, so usage and error messages still reach the terminal
	   (only a stream not already redirected: "2> file" and pipes keep theirs) */
	int out = !GetStdHandle(STD_OUTPUT_HANDLE), err = !GetStdHandle(STD_ERROR_HANDLE);
	if (AttachConsole(ATTACH_PARENT_PROCESS)) { if (out) freopen("CONOUT$", "w", stdout); if (err) freopen("CONOUT$", "w", stderr); }
#endif
	if (argc > 2 + tty) { fprintf(stderr, "usage: homeros [-t] [FILE]\n"); return 2; }
#if defined _WIN32
	if (!tty) b = &backend_win;
#elif defined HOMEROS_WL
	if (!tty && getenv("WAYLAND_DISPLAY")) b = &backend_wl;
#endif
	/* no file: an empty buffer without a name, under the dashboard */
	const char *file = argc > 1 + tty ? argv[1 + tty] : NULL;
	if (shada_read()) { fprintf(stderr, "homeros: out of memory\n"); return 1; }
	if (core_open(&e, file)) { fprintf(stderr, "homeros: %s\n", e.msg); return 1; }
	if (b->open()) { fprintf(stderr, "homeros: no terminal or window\n"); core_free(&e); return 1; }
	e.clip_set = b->clip_set;
	e.clip_get = b->clip_get;
	for (int i = 0, n = b->fds(fds); i < n; i++) loop_watch(fds[i], on_input, NULL);
	if (e.buf->watch >= 0) loop_watch(e.buf->watch, on_watch, &e);
	b->size(&rows, &cols);
	if (grid_resize(&g, rows, cols)) goto out;
	/* also with a file: the first draw measures "loaded in" for a later
	   :Dashboard (thread 0's, agreed) */
	tools_dash(&e, (long long)t0.tv_sec * 1000000000 + t0.tv_nsec);
	int dirty = 1, syn = e.buf->syn != NULL;   /* syn: a reparse is pending */
	while (!e.quit) {
		if (resized) {
			resized = 0;
			b->size(&rows, &cols);
			if (grid_resize(&g, rows, cols)) break;
			dirty = 1;
		}
		for (int i = 0; i < BATCH && qn && !e.quit && !lost; i++, dirty = 1) {
			int k = q[qh];
			qh = (qh + 1) % QMAX;
			qn--;
			kitten_key();   /* any key sends the save kitten away (vim.on_key) */
			if (k == KEY_CTRL_Z && e.mode == MODE_NORMAL && !tools_term()) {
				b->suspend();
				grid_free(&shown);     /* the terminal was used meanwhile: redraw all */
			} else {
				running = 1;
				keys_input(&e, k, qn > 0);
				running = 0;
				lsp_flush(e.buf->lsp);   /* one didChange per command (Helix) */
				shada_track(&e);
				bridge_update(&e);
			}
			loop_int = 0;
		}
		if (e.quit || lost) break;
		if (dirty) syn = e.buf->syn != NULL;
		if (dirty && b->ready()) {
			/* the cluster store was emptied: shown's numbers mean nothing
			   now, so draw all (Neovim's update_screen, UPD_CLEAR) */
			if (grid_frame()) grid_free(&shown);
			core_draw(&e, &g);
			tools_draw(&e, &g);
			kitten_draw(&e, &g);
			b->present(&g, &shown);
			dirty = 0;
		}
		/* deferred work only once the frame is out and no key waits */
		if (syn && !dirty && !qn) {
			int r = syntax_update(e.buf->syn, e.buf->text, loop_waiting);
			syn = r == 2;
			dirty = r == 1;
		}
		/* the journal is made durable at most a second after a change; only
		   then is there a timeout, so an idle editor is never woken */
		int due = e.buf->journal ? journal_due(e.buf->journal) : -1;
		if (loop_once(qn || syn || (dirty && b->ready()) ? 0 : due)) break;
		if (e.buf->journal && journal_due(e.buf->journal) == 0) journal_sync(e.buf->journal);
		if (core_changed) { core_changed = 0; dirty = 1; }
	}
out:
	git_free();
	bridge_free();
	b->close();
	grid_free(&g);
	grid_free(&shown);
	/* core_free would remove the journals as after a clean exit */
	if (lost) { if (e.buf->journal) journal_sync(e.buf->journal); return 1; }
	shada_write(&e);   /* Neovim writes its ShaDa file at exit (getout()) */
	core_free(&e);
	return 0;
}
