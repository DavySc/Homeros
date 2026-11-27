/* Review of PR 140: what font.c's load() costs for a font file not read
   before in this process: HarfBuzz's blob of the file, then FreeType's
   face from it.  Run: blobprobe.exe file... */
#include <windows.h>
#include <stdio.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb.h>
static double now(void)
{
	LARGE_INTEGER f, t;
	QueryPerformanceFrequency(&f);
	QueryPerformanceCounter(&t);
	return (double)t.QuadPart * 1e6 / (double)f.QuadPart;
}
int main(int argc, char **argv)
{
	FT_Library lib;
	FT_Init_FreeType(&lib);
	for (int i = 1; i < argc; i++) {
		double t0 = now();
		hb_blob_t *b = hb_blob_create_from_file_or_fail(argv[i]);
		double t1 = now();
		unsigned len = 0;
		const char *d = b ? hb_blob_get_data(b, &len) : NULL;
		FT_Face f = NULL;
		int e = d ? FT_New_Memory_Face(lib, (const FT_Byte *)d, (FT_Long)len, 0, &f) : -1;
		double t2 = now();
		if (!e) { FT_Set_Pixel_Sizes(f, 0, 16); FT_Load_Char(f, 0x4E2D, FT_LOAD_TARGET_LIGHT | FT_LOAD_RENDER); }
		double t3 = now();
		printf("%s: %u bytes, blob %.0f us, face %.0f us (error %d), first glyph %.0f us\n", argv[i], len, t1 - t0, t2 - t1, e, t3 - t2);
	}
	return 0;
}
