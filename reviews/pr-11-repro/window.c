/* What a window backend can take from grid.h's faces[]: every colour it
   needs for a cell, or not?  A window has no "terminal default", so it
   needs the theme's text, dim and base colours as well; this prints what
   the table gives for the cells every screen has.
     gcc -Iinclude -o /tmp/window reviews/pr-11-repro/window.c ref/theme.c && /tmp/window */
#include <stdio.h>
#include "grid.h"

static void cell(const char *what, int attr, int face)
{
	const Face *f = &faces[face];
	printf("%-34s fg ", what);
	if (f->style & FACE_FG && attr != ATTR_DIM) printf("%3d %3d %3d", f->fg[0], f->fg[1], f->fg[2]); else printf("not in the table");
	printf(",  bg ");
	if (f->style & FACE_BG) printf("%3d %3d %3d\n", f->bg[0], f->bg[1], f->bg[2]); else printf("not in the table\n");
}

int main(void)
{
	cell("plain text (FACE_NONE)", ATTR_NONE, FACE_NONE);
	cell("line number (ATTR_DIM)", ATTR_DIM, FACE_NONE);
	cell("status line (ATTR_REV)", ATTR_REV, FACE_NONE);
	cell("a keyword", ATTR_NONE, FACE_KEYWORD);
	cell("markup.underline", ATTR_NONE, FACE_MARKUP_UNDERLINE);
	cell("comment.error", ATTR_NONE, FACE_COMMENT_ERROR);
	return 0;
}
