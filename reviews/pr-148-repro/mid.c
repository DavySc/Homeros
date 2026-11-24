#include <stdio.h>
#include <string.h>
size_t lsp_u16_byte(const unsigned char *s, size_t n, size_t c);
int main(void)
{
	const unsigned char *b = (const unsigned char *)"\xF0\x9D\x94\xB8xy"; size_t n = 6;
	/* tokens at UTF-16 columns 1 (inside the pair) and 2 ('x'), as decode() walks them */
	size_t pb = 0, pc = 0, cs[] = { 1, 2 };
	for (int i = 0; i < 2; i++) { pb += lsp_u16_byte(b + pb, n - pb, cs[i] - pc); pc = cs[i]; printf("col %zu: walked to byte %zu, from the line start byte %zu\n", cs[i], pb, lsp_u16_byte(b, n, cs[i])); }
	return 0;
}
