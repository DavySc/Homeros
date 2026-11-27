/* the index as syntax_open makes it: Lean reads by lines */
#include "lean.h"
static Tsql *mk(const Text *t, int lang)
{
	Tsql *q = tsql_new(t, lang);
	if (q && lang == 3) tsql_lines(q, lean_line);
	return q;
}
