/* Private to src/file/: the files kept in $XDG_STATE_HOME/homeros/<kind>/
   (Neovim keeps swap and backup files in its state directory, so read-only
   directories and git trees get none), each named by the edited file's
   absolute path with '/' as '%', as Neovim's "//" names them. */
#ifndef FILE_STATE_H
#define FILE_STATE_H
#include <stddef.h>
/* Opens (with flags, mode 0600) the kind file for the absolute path abs,
   creating the directories; its name goes to out.  fd, or -1 (errno set). */
int state_open(const char *kind, const char *abs, char *out, size_t outsz, int flags);
int file_write_all(int fd, const char *b, size_t n);   /* 0 ok, -1 error */
#endif
