/* Drives undo.h for tools/undo_conform.py: one op per line on stdin, a line
   "-" starts a new sequence; prints the state number and the text (lines
   joined by '|') after each op.  Ops: "e R" puts a line "K" above line
   R mod (lines + 1) (Vim's :Nput), "d R" deletes line R mod lines (when more
   than one), "c R" puts "x" at the start of line R mod lines, "u", "r"
   (Ctrl-R), "z R" changes line R mod lines to itself (:s/^//), "w" (a write), "t N" (the clock moves N seconds), and moves
   "j N" (:undo N), "n N" (N changes: g- is -1), "s N" (seconds), "f N"
   (writes).  The clock is this file's time(), which undo.c calls. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "undo.h"

static time_t now = 1700000000;
time_t time(time_t *t) { if (t) *t = now; return now; }

static void edit(Text *t, Undo *u, size_t pos, size_t ndel, const char *ins, size_t nins)
{
	char del[64];
	if (ndel > sizeof del || text_read(t, pos, del, ndel) != ndel || text_delete(t, pos, ndel)
	    || text_insert(t, pos, ins, nins) || undo_record(u, pos, del, ndel, ins, nins, (UndoCursor){ 0, 0, 0 }, 0))
		abort();
	undo_close(u);
}

int main(void)
{
	char line[64], buf[1 << 16];
	Text *t = NULL;
	Undo *u = NULL;
	long k = 0;
	UndoWhere w;
	while (fgets(line, sizeof line, stdin)) {
		char op = line[0];
		long n = atol(line + 1);
		if (op == '-') {
			undo_free(u); text_free(t);
			t = text_new("\n", 1); u = undo_new(); k = 0;
			if (!t || !u) abort();
			continue;
		}
		size_t lines = text_lines(t);
		int r = 0;
		if (op == 'e') {
			char s[32];
			int m = snprintf(s, sizeof s, "%ld\n", ++k);
			edit(t, u, text_line_start(t, (size_t)n % (lines + 1)), 0, s, (size_t)m);
		} else if (op == 'd') {
			if (lines > 1) {
				size_t l = (size_t)n % lines, a = text_line_start(t, l);
				edit(t, u, a, text_line_start(t, l + 1) - a, "", 0);
			}
		} else if (op == 'c') edit(t, u, text_line_start(t, (size_t)n % lines), 0, "x", 1);
		else if (op == 'z') edit(t, u, text_line_start(t, (size_t)n % lines), 0, "", 0);
		else if (op == 'u') r = undo_undo(u, t, &w);
		else if (op == 'r') r = undo_redo(u, t, &w);
		else if (op == 'w') undo_saved(u);
		else if (op == 't') now += n;
		else {
			r = undo_time(u, n, op == 'j' ? UNDO_ABS : op == 'n' ? UNDO_SEQ : op == 's' ? UNDO_SEC : UNDO_FILE);
			if (r > 0) while ((r = undo_step(u, t, &w)) > 0) {}
		}
		if (r < 0 && op != 'j') abort();
		size_t len = text_read(t, 0, buf, sizeof buf - 1);
		for (size_t i = 0; i < len; i++) if (buf[i] == '\n') buf[i] = '|';
		printf("%lu %.*s\n", undo_seq(u), (int)(len ? len - 1 : 0), buf);
	}
	undo_free(u); text_free(t);
	return 0;
}
