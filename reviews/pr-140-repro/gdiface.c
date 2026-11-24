/* The face GDI gives main's backend for "JetBrains Mono" (is it installed for GDI?) */
#include <windows.h>
#include <stdio.h>
int main(void)
{
	HDC dc = CreateCompatibleDC(NULL);
	HFONT f = CreateFontW(-16, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
	                      ANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN, L"JetBrains Mono");
	WCHAR face[LF_FACESIZE];
	SelectObject(dc, f);
	GetTextFaceW(dc, LF_FACESIZE, face);
	printf("GDI face: %ls\n", face);
	return 0;
}
