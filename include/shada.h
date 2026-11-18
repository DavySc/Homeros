/* Recent files: Neovim's v:oldfiles, which its ShaDa file keeps across
   sessions (the "'100" of the default 'shada': the 100 files most recently
   left, newest first).  src/shada/ keeps them as lines "seconds path" in
   the state directory, beside the journals (file.h): oldfiles in
   $XDG_STATE_HOME/homeros/ (default ~/.local/state), %LOCALAPPDATA%\homeros-data\
   on Windows.  As Neovim, a buffer counts once a window has left it (its '"
   mark), at the time it was left, or at exit for one still shown; files under
   /tmp/ and /private/ are never kept (the default 'shada' "r/tmp/,r/private/").
   Two editors exiting at the same moment each read, merge and replace the
   file, so one may lose the other's files, as two Neovims can. */
#ifndef SHADA_H
#define SHADA_H
#include <stddef.h>
struct Editor;

/* At startup: reads the file once (v:oldfiles is not read again).  0 ok,
   also with no file yet; -1 out of memory. */
int  shada_read(void);
/* v:oldfiles[i] (absolute paths, newest first), NULL past the end. */
const char *shada_oldfile(size_t i);
/* After each command: notes the time the current window's buffer was left,
   when it is not the one it was at the last call. */
void shada_track(const struct Editor *e);
/* At exit: the buffers still shown are left now; merged with what the file
   holds now (another editor may have written since) and written.  0 ok,
   -1 error. */
int  shada_write(const struct Editor *e);
/* Neovim's stdpath(state ? "state" : "data") for homeros, followed by name
   (which starts with the separator), into f, n bytes, truncated. */
void shada_stdpath(char *f, size_t n, int state, const char *name);
/* mkdir -p of the directories in f (a file's path), private. */
void shada_mkdirs(char *f);
#endif
