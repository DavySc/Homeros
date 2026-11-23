/* find.h on Windows with DirectWrite, as WezTerm (its dwrite locator) and
   Windows Terminal find fonts: the main font is the first of the families
   asked that is installed, for the machine or the user; a character it
   lacks goes to the font Windows' own fallback list gives
   (IDWriteFontFallback::MapCharacters, Windows 8.1 and up), so ✦ and ↻
   come from Segoe UI Symbol and 😀 from Segoe UI Emoji, as in every other
   Windows program.  The fonts are numbered as they are found; DirectWrite
   hands over each one's file, which FreeType and HarfBuzz then read as on
   Linux.  Empty without _WIN32 (fc.c is the finder there). */
#ifdef _WIN32
#define COBJMACROS
#include <windows.h>
#include <initguid.h>   /* the interfaces' IIDs, which no import library has */
#include <dwrite_2.h>
#include <stdlib.h>
#include <string.h>
#include "find.h"

enum { MAX = 64, TEXT = 64 };   /* fonts found; UTF-16 units of one question */

static IDWriteFactory2 *fac;
static IDWriteFontCollection *sys;
static IDWriteFontFallback *fb;
static WCHAR fam[256], loc[LOCALE_NAME_MAX_LENGTH];   /* the main family; the user's locale (CJK's choice) */
static struct { char *file; int index; } font[MAX];
static int n;

/* The question's text, as MapCharacters reads it through this object; it
   lives as long as the program and is never freed, so its count is 1 */
static WCHAR text[TEXT];
static UINT32 len;

static HRESULT STDMETHODCALLTYPE qi(IDWriteTextAnalysisSource *s, REFIID id, void **out)
{
	*out = IsEqualIID(id, &IID_IUnknown) || IsEqualIID(id, &IID_IDWriteTextAnalysisSource) ? s : NULL;
	return *out ? S_OK : E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE ref(IDWriteTextAnalysisSource *s) { (void)s; return 1; }
static HRESULT STDMETHODCALLTYPE at(IDWriteTextAnalysisSource *s, UINT32 p, const WCHAR **t, UINT32 *l)
{
	(void)s;
	*t = p < len ? text + p : NULL;
	*l = p < len ? len - p : 0;
	return S_OK;
}
static HRESULT STDMETHODCALLTYPE before(IDWriteTextAnalysisSource *s, UINT32 p, const WCHAR **t, UINT32 *l)
{
	(void)s;
	*t = p && p <= len ? text : NULL;
	*l = p <= len ? p : 0;
	return S_OK;
}
static DWRITE_READING_DIRECTION STDMETHODCALLTYPE dir(IDWriteTextAnalysisSource *s) { (void)s; return DWRITE_READING_DIRECTION_LEFT_TO_RIGHT; }
static HRESULT STDMETHODCALLTYPE locale(IDWriteTextAnalysisSource *s, UINT32 p, UINT32 *l, const WCHAR **name)
{
	(void)s;
	*l = p < len ? len - p : 0;
	*name = loc;
	return S_OK;
}
static HRESULT STDMETHODCALLTYPE digits(IDWriteTextAnalysisSource *s, UINT32 p, UINT32 *l, IDWriteNumberSubstitution **ns)
{
	(void)s;
	*l = p < len ? len - p : 0;
	*ns = NULL;
	return S_OK;
}
static IDWriteTextAnalysisSourceVtbl vt = { qi, ref, ref, at, before, dir, locale, digits };
static IDWriteTextAnalysisSource src = { &vt };

/* The number of f, numbered now if it is new; -1 if its file is not a
   local one (no path to give FreeType), the table is full, or color is
   asked and f is not a colour font (which then takes no number) */
static int add(IDWriteFont *f, int color)
{
	IDWriteFontFace *ff = NULL;
	IDWriteFontFile *file = NULL;
	IDWriteFontFileLoader *ld = NULL;
	IDWriteLocalFontFileLoader *local = NULL;
	IDWriteFontFace2 *f2 = NULL;
	UINT32 one = 1, keyn, pathn;
	const void *key;
	int i = -1;
	WCHAR path[MAX_PATH * 4];
	char u[MAX_PATH * 12];
	if (FAILED(IDWriteFont_CreateFontFace(f, &ff)) || FAILED(IDWriteFontFace_GetFiles(ff, &one, &file))
	    || FAILED(IDWriteFontFile_GetLoader(file, &ld))
	    || FAILED(IDWriteFontFileLoader_QueryInterface(ld, &IID_IDWriteLocalFontFileLoader, (void **)&local))
	    || FAILED(IDWriteFontFile_GetReferenceKey(file, &key, &keyn))
	    || FAILED(IDWriteLocalFontFileLoader_GetFilePathLengthFromKey(local, key, keyn, &pathn)) || pathn >= MAX_PATH * 4
	    || FAILED(IDWriteLocalFontFileLoader_GetFilePathFromKey(local, key, keyn, path, MAX_PATH * 4))
	    || !WideCharToMultiByte(CP_UTF8, 0, path, -1, u, sizeof u, NULL, NULL))
		goto done;
	int col = SUCCEEDED(IDWriteFontFace_QueryInterface(ff, &IID_IDWriteFontFace2, (void **)&f2)) && IDWriteFontFace2_IsColorFont(f2);
	if (color && !col) goto done;
	int idx = (int)IDWriteFontFace_GetIndex(ff);
	for (i = 0; i < n && (font[i].index != idx || strcmp(font[i].file, u)); i++) {}
	if (i < n) goto done;
	if (n == MAX || !(font[n].file = malloc(strlen(u) + 1))) { i = -1; goto done; }
	strcpy(font[n].file, u);
	font[n].index = idx;
	i = n++;
done:
	if (f2) IDWriteFontFace2_Release(f2);
	if (local) IDWriteLocalFontFileLoader_Release(local);
	if (ld) IDWriteFontFileLoader_Release(ld);
	if (file) IDWriteFontFile_Release(file);
	if (ff) IDWriteFontFace_Release(ff);
	return i;
}

/* The font Windows' fallback gives for all of the question, from family
   fam first (Segoe UI Emoji if color, as the question has lost its
   U+FE0F); failing that, as WezTerm does, the first installed family (in
   the collection's order) whose regular font has all of it, so a
   character outside Windows' list still draws if any font has it; colour
   fonts only if color, and not by the scan, which on Windows took 0.46 s
   for one ©️ (reviews/pr-140.md); -1 if none */
static int map(const unsigned *cp, unsigned k, int color)
{
	IDWriteFont *f = NULL;
	UINT32 got = 0;
	FLOAT scale;
	int i = -1;
	if (SUCCEEDED(IDWriteFontFallback_MapCharacters(fb, &src, 0, len, sys, color ? L"Segoe UI Emoji" : fam, DWRITE_FONT_WEIGHT_REGULAR,
	                                                 DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, &got, &f, &scale))
	    && f && got == len)
		i = add(f, color);
	if (f) IDWriteFont_Release(f);
	for (UINT32 at = 0, nf = IDWriteFontCollection_GetFontFamilyCount(sys); i < 0 && !color && at < nf; at++) {
		IDWriteFontFamily *ff;
		if (FAILED(IDWriteFontCollection_GetFontFamily(sys, at, &ff))) continue;
		if (SUCCEEDED(IDWriteFontFamily_GetFirstMatchingFont(ff, DWRITE_FONT_WEIGHT_REGULAR, DWRITE_FONT_STRETCH_NORMAL,
		                                                     DWRITE_FONT_STYLE_NORMAL, &f))) {
			BOOL has = TRUE;
			for (unsigned j = 0; j < k && has; j++)
				if (FAILED(IDWriteFont_HasCharacter(f, cp[j], &has))) has = FALSE;
			if (has) i = add(f, color);
			IDWriteFont_Release(f);
		}
		IDWriteFontFamily_Release(ff);
	}
	return i;
}

int find_open(const char *family, int px)
{
	(void)px;   /* DirectWrite's fonts are outlines: any size */
	find_close();
	if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, &IID_IDWriteFactory2, (IUnknown **)&fac))
	    || FAILED(IDWriteFactory2_GetSystemFontCollection(fac, &sys, FALSE))
	    || FAILED(IDWriteFactory2_GetSystemFontFallback(fac, &fb)))
		return -1;
	if (!GetUserDefaultLocaleName(loc, LOCALE_NAME_MAX_LENGTH)) wcscpy(loc, L"en-us");
	for (const char *s = family; *s && !n; s += *s == ',') {   /* the first family installed */
		size_t k = strcspn(s, ",");
		int m = MultiByteToWideChar(CP_UTF8, 0, s, (int)k, fam, 255);
		fam[m] = 0;
		s += k;
		UINT32 at;
		BOOL ok;
		IDWriteFontFamily *ff;
		IDWriteFont *f;
		if (FAILED(IDWriteFontCollection_FindFamilyName(sys, fam, &at, &ok)) || !ok
		    || FAILED(IDWriteFontCollection_GetFontFamily(sys, at, &ff)))
			continue;
		if (SUCCEEDED(IDWriteFontFamily_GetFirstMatchingFont(ff, DWRITE_FONT_WEIGHT_REGULAR, DWRITE_FONT_STRETCH_NORMAL,
		                                                     DWRITE_FONT_STYLE_NORMAL, &f))) {
			add(f, 0);
			IDWriteFont_Release(f);
		}
		IDWriteFontFamily_Release(ff);
	}
	if (!n) {   /* none installed: whatever Windows draws an M with */
		static const unsigned m = 'M';
		fam[0] = 0;
		text[0] = 'M';
		len = 1;
		map(&m, 1, 0);
	}
	return n ? MAX : -1;
}

void find_close(void)
{
	for (int i = 0; i < n; i++) free(font[i].file);
	n = 0;
	if (fb) IDWriteFontFallback_Release(fb);
	if (sys) IDWriteFontCollection_Release(sys);
	if (fac) IDWriteFactory2_Release(fac);
	fb = NULL;
	sys = NULL;
	fac = NULL;
}

const char *find_file(int i, int *index)
{
	*index = i < n ? font[i].index : 0;
	return i < n ? font[i].file : NULL;
}

int find_font(const unsigned *cp, unsigned k, int after, int color)
{
	len = 0;
	for (unsigned j = 0; j < k; j++) {
		if (len + 2 > TEXT) return -1;
		if (cp[j] >= 0x10000) {   /* UTF-16 surrogates */
			text[len++] = (WCHAR)(0xD800 + (cp[j] - 0x10000) / 0x400);
			text[len++] = (WCHAR)(0xDC00 + (cp[j] - 0x10000) % 0x400);
		} else text[len++] = (WCHAR)cp[j];
	}
	int i = len ? map(cp, k, color) : -1;
	return i > after ? i : -1;
}
#endif
