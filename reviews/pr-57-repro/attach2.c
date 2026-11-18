/* main.c's first lines on Windows at 94e50e1 (PR 57, second pass), alone. */
#include <stdio.h>
#include <windows.h>
int main(int argc, char **argv)
{
	(void)argv;
#ifndef BASE   /* -DBASE: origin/main, without the PR's lines */
	int out = !GetStdHandle(STD_OUTPUT_HANDLE), err = !GetStdHandle(STD_ERROR_HANDLE);
	if (AttachConsole(ATTACH_PARENT_PROCESS)) { if (out) freopen("CONOUT$", "w", stdout); if (err) freopen("CONOUT$", "w", stderr); }
#endif
	if (argc != 2) { fprintf(stderr, "usage: homeros [-t] FILE\n"); return 2; }
	return 0;
}
