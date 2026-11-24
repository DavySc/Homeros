/* The editor's main loop, Render.tla "ok": draw only between commands, at
   most BATCH commands per frame, and only when the backend says a frame may
   be drawn (the compositor's frame callback, Present.tla).  The window
   backends show each tab page in its own OS window (Emacs's frames), each
   with its own grids, dirty flag and frame callback (Frames.tla "ok"); the
   terminal shows the current one.  Which tab page a key acts on: Focus.tla
   "asked". */
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

enum { BATCH = 64, QMAX = 4096, FMAX = 32, AMAX = 64 };

static const Backend *b = &backend_tty;
static Editor e;
static int q[QMAX], qw[QMAX], qh, qn;   /* keys read but not yet run, and their windows */
static int running;            /* inside a command */
static int lost;               /* the backend's input is gone for good */
/* an OS window per tab page (the terminal: one, id 0, showing the current) */
static struct { int id, dirty, resized; Grid g, shown; } fr[FMAX];
static int nfr;
/* Focus.tla: the windows raised whose sync's done (KEY_RAISED) has not
   come, oldest first, and the last one raised */
static int asked[AMAX], na, wanted = -1;

static Tab *tab_of(int id)
{
	if (e.tabline) return e.tab;
	for (Tab *t = e.tabs; t; t = t->next) if (t->id == id) return t;
	return NULL;
}

/* fr[i] takes its window's size; 0 ok, -1 out of memory */
static int size(int i)
{
	int rows, cols;
	b->size(fr[i].id, &rows, &cols);
	fr[i].resized = 0;
	fr[i].dirty = 1;
	return grid_resize(&fr[i].g, rows, cols);
}

/* Opens a window for each new tab page and closes the window of each gone
   one; a tab page whose window cannot open closes again (E: in msg).  0 ok,
   -1 out of memory. */
static int windows(void)
{
	for (int i = 0; i < nfr;)
		if (tab_of(fr[i].id)) i++;
		else {
			b->win_close(fr[i].id);
			grid_free(&fr[i].g);
			grid_free(&fr[i].shown);
			fr[i] = fr[--nfr];
			memset(&fr[nfr], 0, sizeof fr[nfr]);   /* its grids are fr[i]'s now */
		}
	for (Tab *t = e.tabline ? NULL : e.tabs, *n; t; t = n) {
		n = t->next;
		int i = 0;
		while (i < nfr && fr[i].id != t->id) i++;
		if (i < nfr) continue;
		if (nfr == FMAX || b->win_open(t->id)) {
			core_tabclose(&e, t);
			snprintf(e.msg, sizeof e.msg, "E: no window for another tab page");
			continue;
		}
		fr[nfr].id = t->id;
		if (size(nfr++)) return -1;
	}
	return 0;
}

static void on_input(void *ctx, int fd)
{
	(void)ctx; (void)fd;
	int k[256], from[256];
	/* Between commands the loop drains the queue, so a full queue leaves the
	   input waiting in the terminal and nothing is lost.  Inside a command
	   (loop_breakcheck) it reads on, so a Ctrl-C behind a long paste still
	   stops the command; keys that do not fit then are dropped. */
	int room = QMAX - qn;
	if (!room && !running) return;
	int n = b->input(k, from, room && room < 256 ? room : 256);
	if (n < 0) { lost = 1; loop_int = 1; return; }
	for (int i = 0; i < n; i++) {
		if (k[i] == KEY_RESIZE) {
			for (int j = 0; j < nfr; j++) fr[j].resized |= fr[j].id == from[i] || e.tabline;
			continue;
		}
		/* Neovim's got_int; the keys typed before it go (Vim's
		   fill_input_buf() drops what was typed before a CTRL-C) */
		if (k[i] == KEY_CTRL_C) { loop_int = 1; qn = 0; }
		if (qn < QMAX) q[(qh + qn) % QMAX] = k[i], qw[(qh + qn++) % QMAX] = from[i];
	}
}

/* One key from window w.  A focus change acts as Focus.tla's rule says;
   KEY_CLOSE closes its window's tab page, the last one as :qa (keys); any
   other key acts on the current tab page, wherever it was typed. */
static void key(int k, int w)
{
	int was = e.tab->id, i = 0;
	Tab *t = tab_of(w);
	if (k == KEY_RAISED) {
		if (na) memmove(asked, asked + 1, (size_t)--na * sizeof *asked);
		return;
	}
	if (k == KEY_FOCUS) {
		while (i < na && asked[i] != w) i++;
		if (!t || (w != wanted && i < na)) return;   /* answers an earlier raise */
		if (w != wanted) keys_input(&e, k, 0);          /* the user's: half-done work ends */
		core_tab_enter(&e, t);
		return;
	}
	if (k == KEY_CLOSE && t && e.tabs->next && !e.tabline) core_tabclose(&e, t);
	else keys_input(&e, k, qn > 0);
	/* the key moved to another tab page (SPC 5 o, gt): ask the system to
	   follow, and keep the answers apart */
	if (e.tab->id != was && !e.tabline && !windows() && na < AMAX) {
		asked[na++] = wanted = e.tab->id;
		b->raise(wanted);
	}
}

int main(int argc, char **argv)
{
	struct timespec t0;
	timespec_get(&t0, TIME_UTC);   /* for the dashboard's "loaded in" */
	int fds[2], tty = argc > 1 && !strcmp(argv[1], "-t");
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
	if (b->open() || b->win_open(0)) { fprintf(stderr, "homeros: no terminal or window\n"); core_free(&e); return 1; }
	e.clip_set = b->clip_set;
	e.clip_get = b->clip_get;
	e.tabline = b == &backend_tty;
	for (int i = 0, n = b->fds(fds); i < n; i++) loop_watch(fds[i], on_input, NULL);
	fr[nfr++].id = 0;
	if (size(0)) goto out;
	/* also with a file: the first draw measures "loaded in" for a later
	   :Dashboard (thread 0's, agreed) */
	tools_dash(&e, (long long)t0.tv_sec * 1000000000 + t0.tv_nsec);
	int dirty = 1, syn = e.buf->syn != NULL;   /* dirty: a command ran; syn: a reparse is pending */
	while (!e.quit) {
		for (int i = 0; i < nfr; i++) if (fr[i].resized && size(i)) goto out;
		for (int i = 0; i < BATCH && qn && !e.quit && !lost; i++, dirty = 1) {
			int k = q[qh], w = qw[qh];
			qh = (qh + 1) % QMAX;
			qn--;
			kitten_key();   /* any key sends the save kitten away (vim.on_key) */
			if (k == KEY_CTRL_Z && e.mode == MODE_NORMAL && !tools_term()) {
				b->suspend();
				grid_free(&fr[0].shown);   /* the terminal was used meanwhile: redraw all */
			} else {
				running = 1;
				key(k, w);
				running = 0;
				lsp_flush(e.buf->lsp);   /* one didChange per command (Helix) */
				shada_track(&e);
				bridge_update(&e);
				lsp_goals(&e);
			}
			loop_int = 0;
		}
		if (e.quit || lost || windows()) break;
		if (dirty) syn = e.buf->syn != NULL;
		int wait = 0;   /* the current tab page's frame waits for its callback */
		for (int i = 0; i < nfr; i++) {
			/* an edit may show in every tab page (one buffer in two frames) */
			Tab *t = tab_of(fr[i].id);
			if (!(fr[i].dirty |= dirty) || !b->ready(fr[i].id)) { wait |= fr[i].dirty && t == e.tab; continue; }
			/* the cluster store was emptied: every grid's numbers mean
			   nothing now, so all draw again (Neovim's update_screen,
			   UPD_CLEAR) */
			if (grid_frame()) for (int j = 0; j < nfr; j++) grid_free(&fr[j].shown), fr[j].dirty = 1;
			Grid *g = &fr[i].g;
			core_draw(&e, t, g);
			if (t == e.tab) tools_draw(&e, g), keys_draw(&e, g), kitten_draw(&e, g);
			b->present(fr[i].id, g, &fr[i].shown);
			fr[i].dirty = 0;
		}
		dirty = 0;
		/* deferred work only once the frame is out and no key waits */
		if (syn && !wait && !qn) {
			size_t r1, r2;
			int r = syntax_update(e.buf->syn, e.buf->text, loop_waiting, &r1, &r2);
			syn = r == 2;
			dirty = r == 1;
		}
		int draw = 0;
		for (int i = 0; i < nfr; i++) draw |= (fr[i].dirty || dirty) && b->ready(fr[i].id);
		/* the journal is made durable at most a second after a change; only
		   then is there a timeout, so an idle editor is never woken */
		int due = e.buf->journal ? journal_due(e.buf->journal) : -1;
		if (loop_once(qn || syn || draw ? 0 : due)) break;
		if (e.buf->journal && journal_due(e.buf->journal) == 0) journal_sync(e.buf->journal);
		if (core_changed) { core_changed = 0; dirty = 1; }
	}
out:
	git_free();
	bridge_free();
	b->close();
	for (int i = 0; i < nfr; i++) grid_free(&fr[i].g), grid_free(&fr[i].shown);
	/* core_free would remove the journals as after a clean exit */
	if (lost) { if (e.buf->journal) journal_sync(e.buf->journal); return 1; }
	shada_write(&e);   /* Neovim writes its ShaDa file at exit (getout()) */
	core_free(&e);
	return 0;
}
