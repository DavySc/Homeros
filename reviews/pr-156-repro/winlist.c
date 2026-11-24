/* PR 156 review: how many visible "homeros" frames, in how many homeros.exe
   processes, and which window is in front.  "keys TEXT": type TEXT into the
   foreground window (WM_CHAR; \r Enter). */
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <string.h>
int main(int argc, char **argv)
{
	if (argc == 3 && !strcmp(argv[1], "keys")) {
		HWND h = NULL, last = NULL;
		while ((h = FindWindowExW(NULL, h, L"homeros", NULL))) if (IsWindowVisible(h)) last = h;
		HWND f = GetForegroundWindow();
		wchar_t c[64] = L"";
		if (f) GetClassNameW(f, c, 64);
		if (wcscmp(c, L"homeros")) f = last;
		for (const char *s = argv[2]; f && *s; s++) { PostMessageW(f, WM_CHAR, *s == '~' ? '\r' : (unsigned char)*s, 0); Sleep(30); }
		return 0;
	}
	int frames = 0, procs = 0;
	for (HWND h = NULL; (h = FindWindowExW(NULL, h, L"homeros", NULL));) frames += IsWindowVisible(h) != 0;
	HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	PROCESSENTRY32 pe = { .dwSize = sizeof pe };
	for (BOOL ok = Process32First(s, &pe); ok; ok = Process32Next(s, &pe)) procs += !_stricmp(pe.szExeFile, "homeros.exe");
	wchar_t c[64] = L"none";
	HWND f = GetForegroundWindow();
	if (f) GetClassNameW(f, c, 64);
	printf("frames %d, homeros.exe processes %d, remote window %s, in front: %ls\n", frames, procs,
	       FindWindowExW(HWND_MESSAGE, NULL, L"homeros-remote", NULL) ? "yes" : "no", c);
	return 0;
}
