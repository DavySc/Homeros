/* Neovim's key notation as the tests write keys: <BS> <Tab> <C-r> and the
   other names below, the rest UTF-8, one character a key (thread 0's,
   agreed).  *s must not be at the end. */
#include <string.h>
#include "keys.h"
#include "uni.h"
static inline int test_key(const char **s)
{
	static const struct { const char *n; int k; } named[] = {
		{ "<BS>", KEY_BS }, { "<Del>", KEY_DEL }, { "<Tab>", KEY_TAB }, { "<S-Tab>", KEY_S_TAB },
		{ "<Esc>", KEY_ESC }, { "<CR>", KEY_ENTER }, { "<Space>", ' ' }, { "<Left>", KEY_LEFT }, { "<Right>", KEY_RIGHT },
	};
	for (size_t i = 0; i < sizeof named / sizeof *named; i++) {
		size_t l = strlen(named[i].n);
		if (!strncmp(*s, named[i].n, l)) { *s += l; return named[i].k; }
	}
	const char *c = *s;
	if (!strncmp(c, "<C-", 3) && c[3] >= 'a' && c[3] <= 'z' && c[4] == '>') { *s += 5; return c[3] - 'a' + 1; }
	const unsigned char *u = (const unsigned char *)c;
	size_t n = strlen(c);
	*s += uni_len(u, n, 0);
	return (int)uni_cp(u, n, 0);
}
