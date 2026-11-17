/* Does the tree implement its own headers?  Every function declared in
   include/ is named here; link against ref/ (the Makefile's SRCS). */
#include "backend.h"
#include "core.h"
#include "file.h"
#include "font.h"
#include "loop.h"
#include "syntax.h"
#include "undo.h"
void *const used[] = {
	(void *)loop_timer, (void *)loop_breakcheck, (void *)proc_spawn, (void *)proc_write,
	(void *)proc_close, (void *)proc_kill,
	(void *)undo_time, (void *)undo_step, (void *)undo_saved, (void *)undo_write, (void *)undo_read,
	(void *)file_watch, (void *)file_watch_read, (void *)journal_open, (void *)journal_base,
	(void *)journal_edit, (void *)journal_saved, (void *)journal_due, (void *)journal_sync,
	(void *)journal_close,
	(void *)font_open, (void *)font_close, (void *)font_cell, (void *)font_shape, (void *)font_stats,
};
int main(void) { return !used[0]; }
