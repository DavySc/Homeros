#!/usr/bin/env bash
# Every cell style (3 attributes x 91 faces, and face values past FACE_COUNT)
# must give the same escape from the PR's ref/tty.c style() + ref/theme.c as
# from main's ref/tty.c.  Run from the repo root on the PR head:
#   bash reviews/pr-11-repro/escapes.sh [base-commit, default origin/main]
# Both style() functions are cut out of their files, so this tests the code
# as written, not a copy.
set -e
base=${1:-origin/main}
d=$(mktemp -d)
git show "$base:ref/tty.c" | awk '/^static const char \*const face_fg/,/^};$/ {print} /^static void style/,/^}$/ {print}' \
  | sed 's/static void style/static void style_old/' > "$d/old.inc"
awk '/^static void style/,/^}$/' ref/tty.c | sed 's/static void style/static void style_new/' > "$d/new.inc"
git show "$base:include/grid.h" > "$d/grid_old.h"
cat > "$d/t.c" <<'C'
#include <stdio.h>
#include <string.h>
#include "grid.h"
static char buf[256]; static size_t bn;
static void outs(const char *s) { size_t n = strlen(s); memcpy(buf + bn, s, n + 1); bn += n; }
#include "old.inc"
#include "new.inc"
int main(void)
{
	int bad = 0, n = 0; size_t longest = 0;
	static const unsigned char attrs[] = { ATTR_NONE, ATTR_REV, ATTR_DIM };
	for (int a = 0; a < 3; a++) for (int f = 0; f < 256; f++) {
		Cell x = { " ", 1, attrs[a], (unsigned char)f };
		char o[256];
		bn = 0; style_old(&x); strcpy(o, buf);
		bn = 0; style_new(&x);
		if (bn > longest) longest = bn;
		n++;
		if (strcmp(o, buf)) { bad++; printf("DIFFER attr %d face %d\n", a, f); }
	}
	printf("%d styles compared, %d differ, longest escape %zu bytes\n", n, bad, longest);
	return bad != 0;
}
C
gcc -std=c17 -D_DEFAULT_SOURCE -Wall -Wextra -Werror -Iinclude -I"$d" -g -fsanitize=address,undefined -fno-sanitize-recover=all -o "$d/t" "$d/t.c" ref/theme.c
"$d/t"; rc=$?
rm -rf "$d"; exit $rc
