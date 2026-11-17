/* A backend shows the grid and turns input into keys (keys.h codes).
   ref/tty.c is the terminal mode (changed cells only, synchronized update);
   packages 8 and 9 add the Wayland and Win32 windows, which may draw only
   after the compositor's frame callback (Present.tla): backend_ready says so. */
#ifndef BACKEND_H
#define BACKEND_H
#include "grid.h"

int  backend_open(void);              /* 0 ok, -1 error */
void backend_close(void);
int  backend_fds(int fds[2]);         /* how many fds to poll; then call backend_input */
/* Reads what is available; returns how many keys it put in out (max n).
   A changed size comes as KEY_RESIZE. */
int  backend_input(int *out, int n);
void backend_size(int *rows, int *cols);
int  backend_ready(void);             /* may a frame be drawn now? */
/* Shows cur, sending only cells that differ from shown, then makes shown
   equal to cur.  A shown grid of another size is redrawn whole. */
void backend_present(const Grid *cur, Grid *shown);
void backend_suspend(void);           /* Ctrl-Z: give the terminal back until resumed */
#endif
