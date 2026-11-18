/* For each face and pixel size, draw text with a quality into a 32-bit DIB
   (white on black, as backend.c does: ETO_OPAQUE) and count inked pixels that
   are partial (blended) and partial pixels whose R, G, B differ (fringed).
   partial 0 = bi-level (aliased) text.  usage: smooth.exe */
#include <windows.h>
#include <stdio.h>

static void probe(const wchar_t *face, int px, int q)
{
	HDC dc = CreateCompatibleDC(NULL);
	BITMAPINFO bi = {{sizeof bi.bmiHeader, 400, -40, 1, 32, BI_RGB, 0, 0, 0, 0, 0}, {{0}}};
	unsigned *bits;
	HBITMAP b = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, (void **)&bits, NULL, 0);
	HFONT f = CreateFontW(-px, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
	                      CLIP_DEFAULT_PRECIS, q, FIXED_PITCH | FF_MODERN, face);
	SelectObject(dc, b);
	SelectObject(dc, f);
	wchar_t got[64];
	GetTextFaceW(dc, 64, got);
	SetBkColor(dc, RGB(0, 0, 0));
	SetTextColor(dc, RGB(255, 255, 255));
	RECT r = {0, 0, 400, 40};
	ExtTextOutW(dc, 0, 0, ETO_OPAQUE | ETO_CLIPPED, &r, L"This file contains ### bla", 26, NULL);
	GdiFlush();
	int ink = 0, part = 0, fringe = 0;
	for (int i = 0; i < 400 * 40; i++) {
		unsigned p = bits[i] & 0xffffff, R = p >> 16, G = p >> 8 & 255, B = p & 255;
		if (!p) continue;
		ink++;
		if (p != 0xffffff) part++;
		if (R != G || G != B) fringe++;
	}
	printf("%-16ls -> %-16ls %2dpx q=%d  ink %4d  partial %4d  fringed %4d\n", face, got, px, q, ink, part, fringe);
	DeleteDC(dc); DeleteObject(f); DeleteObject(b);
}

int main(void)
{
	static const wchar_t *faces[] = {L"JetBrains Mono", L"Liberation Mono", L"NoSuchFaceAtAll"};
	static const int sizes[] = {16, 20, 24};
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 3; j++) {
			probe(faces[i], sizes[j], ANTIALIASED_QUALITY);
			probe(faces[i], sizes[j], CLEARTYPE_QUALITY);
		}
	return 0;
}
