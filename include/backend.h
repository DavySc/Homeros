/* A backend shows the grid and turns input into keys (keys.h codes).
   src/tty is the terminal mode (changed cells only, synchronized update);
   packages 8 and 9 add the Wayland and Win32 windows, which may draw only
   after the compositor's frame callback (Present.tla): ready says so.
   Both kinds link into one binary, so each backend is a table and main.c
   picks one: the window by default, the terminal with -t or without a
   display. */
#ifndef BACKEND_H
#define BACKEND_H
#include <stddef.h>
#include "grid.h"

typedef struct {
	int  (*open)(void);              /* 0 ok, -1 error */
	void (*close)(void);
	int  (*fds)(int fds[2]);         /* how many fds to poll; then call input */
	/* Reads what is available; returns how many keys it put in out (max n).
	   A changed size comes as KEY_RESIZE.  -1: the input is gone for good
	   (the terminal hung up, the compositor died); main.c then exits as
	   Neovim does on SIGHUP or a read error (preserve_exit()): nothing is
	   saved, the crash journal is kept for the next open. */
	int  (*input)(int *out, int n);
	void (*size)(int *rows, int *cols);
	int  (*ready)(void);             /* may a frame be drawn now? */
	/* Shows cur, sending only cells that differ from shown, then makes shown
	   equal to cur.  A shown grid of another size is redrawn whole. */
	void (*present)(const Grid *cur, Grid *shown);
	void (*suspend)(void);           /* Ctrl-Z: give the terminal back until resumed */
	/* The system clipboard (the "+ register), synchronous with a timeout as
	   Neovim's clipboard provider is.  set: 0 ok, -1 none.  get: the
	   clipboard's length (only the first max bytes are copied to buf, so a
	   larger result means call again with more room), -1 none. */
	int  (*clip_set)(const char *s, size_t n);
	long (*clip_get)(char *buf, size_t max);
	/* The cell of the last mouse key (KEY_MOUSE_LEFT, KEY_WHEEL_*), as
	   Neovim keeps mouse_row and mouse_col. */
	void (*mouse)(int *row, int *col);
} Backend;

extern const Backend backend_tty;    /* src/tty, package 6 */
extern const Backend backend_wl;     /* src/wl/, package 8; built when src/wl/ exists (HOMEROS_WL) */
extern const Backend backend_win;    /* src/win/, package 9; the Windows build */
#endif
