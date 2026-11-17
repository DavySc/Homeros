/* What Wine does for the two candidate calls, seen from Linux:
     zig cc -target x86_64-windows-gnu -O1 -o replace.exe reviews/pr-25-repro/replace.c
     strace -f -e trace=rename,renameat,renameat2,unlink,unlinkat,link,linkat wine replace.exe replace   (or: move)
   run in a Wine prefix; the files are z:\tmp\rv-file and z:\tmp\rv-temp */
#include <windows.h>
#include <stdio.h>
#include <string.h>
int main(int argc, char **argv)
{
	FILE *f = fopen("z:\\tmp\\rv-file", "wb"); fputs("old\n", f); fclose(f);
	f = fopen("z:\\tmp\\rv-temp", "wb"); fputs("new\n", f); fclose(f);
	Sleep(300);
	fprintf(stderr, "---- the call starts\n");
	BOOL ok = argc > 1 && !strcmp(argv[1], "replace")
	        ? ReplaceFileW(L"z:\\tmp\\rv-file", L"z:\\tmp\\rv-temp", NULL, 0, NULL, NULL)
	        : MoveFileExW(L"z:\\tmp\\rv-temp", L"z:\\tmp\\rv-file", MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
	fprintf(stderr, "---- the call returned %d\n", ok);
	return 0;
}
