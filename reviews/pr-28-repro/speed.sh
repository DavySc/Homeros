#!/bin/sh
# PR 28 review: before (main's text.c, path in $1) against after, 5 alternating
# runs per line length, median (min to max) ns per lookup.  CC=gcc or clang.
CC=${CC:-gcc}; before=$1
$CC -O2 -Iinclude -o /tmp/sp_before reviews/pr-28-repro/speed.c $before
$CC -O2 -Iinclude -o /tmp/sp_after reviews/pr-28-repro/speed.c src/text/text.c
for every in 1 2 4 8 16 31 32 33 50 64 100 200 500 1000 3000; do
	b=""; a=""
	for i in 1 2 3 4 5; do b="$b $(/tmp/sp_before $every)"; a="$a $(/tmp/sp_after $every)"; done
	python3 -c "
import sys
b=sorted(map(float,'$b'.split())); a=sorted(map(float,'$a'.split()))
print('$CC every %5d  before %8.1f (%.1f to %.1f)  after %8.1f (%.1f to %.1f)' % ($every,b[2],b[0],b[4],a[2],a[0],a[4]))"
done
