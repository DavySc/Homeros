/* src/main.c with a scripted window backend: an OS window per tab page,
   each drawn on its own dirty flag and frame callback (Frames.tla "ok":
   Own, NoWaste, NoIdle, Fresh), and the tab page a key acts on (Focus.tla
   "asked": InFrame after two quick switches, a refused raise, a click).
   The script is the system: keys typed into the focused window, focus
   events, the sync's done after a raise, frame callbacks, the close
   button, a file another launch hands over (remote.h: it opens in a new
   frame, raised, even after a :q! in the same batch); checks run between
   events, when main.c has run every key before. */
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "check.h"
/* remote.h's stubs give way to the script's */
#define REMOTE_H
static const char *t_handed;
static int remote_open(const char *file) { (void)file; return 0; }
static char *remote_next(void) { char *s = t_handed ? strdup(t_handed) : NULL; t_handed = NULL; return s; }
static void remote_stop(void) {}
#define main homeros_main
#include "../src/main.c"
#undef main

enum { NW = 8 };
static int t_pipe[2], t_osw;   /* a byte per event; the window the system focuses */
static int t_rows[NW], t_open[NW], t_ready[NW] = { 1, 1, 1, 1, 1, 1, 1, 1 }, t_drawn[NW], t_raise[NW], t_nraise;
/* k: keys s; f: focus a; r: done; c: close a; h, v: hide, show a; s: a's
   new size; o: file s handed over, read with key a (0: none); t: check */
typedef struct { char op; int a; const char *s; } Ev;
static char t_b[64];   /* the file handed over */
static const Ev *t_ev;
static int t_nev, t_pos, t_sub;   /* t_sub: the next byte of a k event */

static int t_open_(void) { return 0; }
static void t_close(void) {}
static int t_win_open(int id) { CHECK(id < NW && !t_open[id]); t_open[id] = 1; return 0; }
static void t_win_close(int id) { CHECK(t_open[id] == 1); t_open[id] = 2; }
static void t_raise_(int id) { CHECK(t_nraise < NW); t_raise[t_nraise++] = id; }
static int t_fds(int fds[2]) { fds[0] = t_pipe[0]; return 1; }
static void t_size(int id, int *rows, int *cols) { *rows = t_rows[id] ? t_rows[id] : 12; *cols = 40; }
static int t_ready_(int id) { return t_ready[id]; }
static void t_present(int id, const Grid *cur, Grid *shown) { (void)cur; (void)shown; CHECK(t_open[id] == 1 && t_ready[id]); t_drawn[id]++; }
static void t_suspend(void) {}
static int t_clip_set(const char *s, size_t n) { (void)s; (void)n; return -1; }
static long t_clip_get(char *buf, size_t max) { (void)buf; (void)max; return -1; }
static void t_mouse(int *row, int *col) { *row = *col = 0; }
static void t_cell(int id, int *w, int *h) { (void)id; *w = *h = 0; }

static void t_check(int n);

/* One event, or the next key of a k event; a check is run first. */
static int t_input(int *out, int *win, int n)
{
	(void)n;
	char c;
	if (t_pos == t_nev) fprintf(stderr, "still running after the script: %s\n", e.msg);
	CHECK(t_pos < t_nev && read(t_pipe[0], &c, 1) == 1);
	while (t_ev[t_pos].op == 't') t_check(t_ev[t_pos++].a);
	const Ev *v = &t_ev[t_pos];
	*win = t_osw;
	switch (v->op) {
	case 'k': *out = (unsigned char)v->s[t_sub++]; if (!v->s[t_sub]) t_sub = 0, t_pos++; return 1;
	case 'f': t_pos++; *out = KEY_FOCUS; *win = t_osw = v->a; return 1;
	case 'r': t_pos++; *out = KEY_RAISED; *win = -1; return 1;
	case 'c': t_pos++; *out = KEY_CLOSE; *win = v->a; return 1;
	case 's': t_pos++; t_rows[v->a] = 20; *out = KEY_RESIZE; *win = v->a; return 1;
	case 'h': case 'v': t_pos++; t_ready[v->a] = v->op == 'v'; return 0;
	case 'o': t_pos++; t_handed = v->s; *out = v->a; return v->a != 0;
	}
	CHECK(0);
	return 0;
}

static const Backend t_backend = { t_open_, t_close, t_win_open, t_win_close, t_raise_, t_fds, t_input, t_size,
	t_ready_, t_present, t_suspend, t_clip_set, t_clip_get, t_mouse, t_cell };

static int t_d0, t_d1;
/* the checks, by number */
static void t_check(int n)
{
	switch (n) {
	case 0: CHECK(t_open[0] == 1 && !t_open[1] && t_drawn[0] && !t_nraise); break;
	/* SPC 5 2: a window for the new tab page, which is raised */
	case 1: CHECK(t_open[1] == 1 && e.tab->id == 1 && t_nraise == 1 && t_raise[0] == 1 && t_drawn[1]); break;
	/* two quick gt: the focus the first raise gives is dropped (InFrame) */
	case 2: CHECK(t_nraise == 3 && t_raise[1] == 0 && t_raise[2] == 1 && e.tab->id == 1); break;
	case 3: CHECK(e.tab->id == 1 && e.mode == MODE_INSERT); break;
	/* both raises refused, then a click on frame 0: the user's, which ends
	   insert mode as Esc would */
	case 4: CHECK(t_nraise == 5 && e.tab->id == 1 && e.mode == MODE_INSERT); break;
	case 5: CHECK(e.tab->id == 0 && e.mode == MODE_NORMAL); t_d0 = t_drawn[0]; t_d1 = t_drawn[1]; break;
	/* frame 1 hidden: frame 0 draws on its own callback (Own), frame 1 not
	   at all (NoWaste) */
	case 6: CHECK(t_drawn[0] > t_d0 && t_drawn[1] == t_d1); t_d0 = t_drawn[0]; break;
	/* nothing typed: nothing drawn (NoIdle) */
	case 7: CHECK(t_drawn[0] == t_d0 && t_drawn[1] == t_d1); break;
	/* shown again: frame 1 draws what changed while it was hidden (Fresh) */
	case 8: CHECK(t_drawn[1] == t_d1 + 1 && t_drawn[0] == t_d0); break;
	/* frame 1 resized: its grid only */
	case 10: CHECK(fr[0].g.rows + fr[1].g.rows == 32 && fr[fr[1].id == 1].g.rows == 20); break;
	/* the close button of frame 1: :tabclose for it, not :qa */
	case 9: CHECK(t_open[1] == 2 && !e.quit && !e.tabs->next && e.tab->id == 0); break;
	/* a file handed over with the Enter of a :q!: the editor stays, and the
	   file is in a new frame, raised */
	case 11: CHECK(!e.quit && e.tabs->next && e.tab->id && t_open[e.tab->id] == 1 && t_raise[t_nraise - 1] == e.tab->id
	               && e.buf->path && strstr(e.buf->path, "b.txt")); t_d0 = e.tab->id; break;
	/* none named: an empty buffer in a third frame; the Visual mode begun
	   before it ended, as on the user's focus change */
	case 12: CHECK(e.tab->id != t_d0 && e.tab->id && t_open[e.tab->id] == 1 && t_raise[t_nraise - 1] == e.tab->id && !e.buf->path
	               && e.mode == MODE_NORMAL && !e.ks.visual); break;
	}
}

int main(void)
{
	char dir[] = "/tmp/homeros-frames-XXXXXX", path[64];
	CHECK(mkdtemp(dir) && !setenv("XDG_STATE_HOME", dir, 1) && !setenv("XDG_DATA_HOME", dir, 1) && !setenv("XDG_CONFIG_HOME", dir, 1));
	snprintf(path, sizeof path, "%s/a.txt", dir);
	FILE *f = fopen(path, "w");
	CHECK(f && fputs("one\ntwo\n", f) >= 0 && !fclose(f));
	unsetenv("WAYLAND_DISPLAY");
	static const Ev ev[] = {
		{ 't', 0, 0 },
		{ 'k', 0, " 52" }, { 'f', 1, 0 }, { 'r', 0, 0 }, { 't', 1, 0 },
		{ 'k', 0, "gt" }, { 'k', 0, "gt" }, { 'f', 0, 0 }, { 't', 2, 0 },
		{ 'r', 0, 0 }, { 'f', 1, 0 }, { 'r', 0, 0 }, { 'k', 0, "i" }, { 't', 3, 0 },
		{ 'k', 0, "\x1bgtgti" }, { 'r', 0, 0 }, { 'r', 0, 0 }, { 't', 4, 0 },
		{ 'f', 0, 0 }, { 't', 5, 0 },
		{ 'h', 1, 0 }, { 'k', 0, "ix\x1b" }, { 't', 6, 0 },
		{ 'v', 0, 0 }, { 't', 7, 0 },
		{ 'v', 1, 0 }, { 't', 8, 0 },
		{ 's', 1, 0 }, { 't', 10, 0 },
		{ 'c', 1, 0 }, { 't', 9, 0 },
		{ 'k', 0, ":q!" }, { 'o', '\r', t_b }, { 't', 11, 0 },
		{ 'k', 0, "v" }, { 'o', 0, "" }, { 't', 12, 0 },
		{ 'k', 0, ":q!\r:q!\r:q!\r" },
	};
	snprintf(t_b, sizeof t_b, "%s/b.txt", dir);
	t_ev = ev;
	t_nev = sizeof ev / sizeof *ev;
	CHECK(!pipe(t_pipe));
	/* a byte per key or event; the checks run here, so they take none */
	int bytes = 0;
	for (int i = 0; i < t_nev; i++) bytes += ev[i].op == 'k' ? (int)strlen(ev[i].s) : ev[i].op != 't';
	for (int i = 0; i < bytes; i++) CHECK(write(t_pipe[1], "x", 1) == 1);
	b = &t_backend;
	char *argv[] = { "homeros", path, NULL };
	/* the checks run when the event after them is read: main.c has run
	   every key before by then (it reads between commands, one key a call) */
	CHECK(homeros_main(2, argv) == 0);
	CHECK(t_pos == t_nev);
	puts("test_frames_main: a window per tab page, raised on gt; a raise's own focus dropped, the user's click taken; "
	     "a hidden frame neither drawn nor holding the others; nothing drawn when idle; a resize only its window's; the close button closes its tab page; "
	     "a file handed over opens in a raised frame, also after a :q!");
	return 0;
}
