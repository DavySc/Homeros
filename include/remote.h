/* One editor per Windows session: a launch while one runs hands its file to
   that one, which opens it in a new frame and comes forward; the launch then
   exits.  Emacs's emacsclient -c (a new frame per file) and Notepad++'s
   single instance (a named mutex, then WM_COPYDATA to the running window),
   so a program that opens files through the file association ("homeros.exe
   FILE") reaches the editor already open.  Remote.tla "ok".  The terminal
   (-t) takes no part.  Windows only (src/win/remote.c); elsewhere no launch
   reaches another yet, and these do nothing. */
#ifndef REMOTE_H
#define REMOTE_H
#if defined _WIN32
/* Before anything else: file as main has it (NULL: none, which opens an
   empty buffer there, as :tabnew).  1: the running editor took it, exit;
   0: run here, as the editor later launches hand theirs to when none ran. */
int   remote_open(const char *file);
/* The next file handed over, absolute ("" for none named), allocated; NULL
   when none waits.  Between commands only: the message that brings one can
   arrive inside a command (loop_breakcheck reads input). */
char *remote_next(void);
/* No more files are taken: after the main loop, before anything reads
   messages again, so none is taken that nothing would open. */
void  remote_stop(void);
#else
static inline int   remote_open(const char *file) { (void)file; return 0; }
static inline char *remote_next(void) { return 0; }
static inline void  remote_stop(void) {}
#endif
#endif
