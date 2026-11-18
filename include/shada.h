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
#include "file.h"
struct Editor;

/* At startup: reads the file once (v:oldfiles is not read again).  0 ok,
   also with no file yet or one that cannot be read; -1 out of memory. */
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
/* The store under both this and the pickers' frecency: lines "t path", t a
   number (seconds), path to the line's end. */
typedef struct { double t; char *path; } ShadaLine;
/* Reads the file name into *v (malloc'd, each path too; *v is NULL with no
   lines), *n lines, and its identity into *id for shada_save.  No file is 0
   lines.  0 ok; -1 error (errno set; ENOMEM: *v holds the lines read so far). */
int  shada_load(const char *name, ShadaLine **v, size_t *n, FileId *id);
/* Replaces the file name with the n lines, making its directories, with
   file_save; when another editor wrote it since *id, ours wins (its copy is
   removed).  *id is updated.  0 ok, -1 error. */
int  shada_save(const char *name, const ShadaLine *v, size_t n, FileId *id);
#endif
