/* main.c's first lines on Windows (PR 57), alone: the usage message with no argument. */
#include <stdio.h>
#include <windows.h>
int main(int argc, char **argv)
{
	(void)argv;
#ifndef BASE   /* -DBASE: origin/main, without the PR's line */
	if (AttachConsole(ATTACH_PARENT_PROCESS)) { freopen("CONOUT$", "w", stdout); freopen("CONOUT$", "w", stderr); }
#endif
	if (argc != 2) { fprintf(stderr, "usage: homeros [-t] FILE\n"); return 2; }
	return 0;
}
