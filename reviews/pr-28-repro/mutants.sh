#!/bin/sh
# PR 28 review: bugs planted in the new block skip; which checks catch each.
# Run from the repo root.  Restores src/text/text.c afterwards.
set -u
f=src/text/text.c; cp $f /tmp/text.c.orig
SAN="-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all"
try() {
	name=$1; from=$2; to=$3
	cp /tmp/text.c.orig $f
	python3 - "$f" "$from" "$to" <<'PY'
import sys
p, a, b = sys.argv[1:]
s = open(p).read()
assert s.count(a) == 1, a
open(p, 'w').write(s.replace(a, b))
PY
	out="$name:"
	gcc -Iinclude $SAN -o /tmp/m_edges reviews/pr-28-repro/edges.c $f && { /tmp/m_edges >/dev/null 2>&1 && out="$out edges MISSED;" || out="$out edges caught;"; }
	for t in text text_rope; do
		gcc -Iinclude -Itests $SAN -o /tmp/m_$t tests/test_$t.c $f 2>/dev/null && { /tmp/m_$t >/dev/null 2>&1 && out="$out test_$t MISSED;" || out="$out test_$t caught;"; }
	done
	echo "$out"
}
try "skips the block that holds the line (>= for >)" 'l > (k = nls(x->b + i, 32))' 'l >= (k = nls(x->b + i, 32))'
try "block step 31" '{ l -= k; i += 32; }' '{ l -= k; i += 31; }'
try "block step 33" '{ l -= k; i += 32; }' '{ l -= k; i += 33; }'
try "counts 31 of the block" 'l > (k = nls(x->b + i, 32))' 'l > (k = nls(x->b + i, 31))'
try "counts past the leaf's bytes (> 0 for > 32)" 'while (x->n - i > 32 &&' 'while (x->n - i > 0 &&'
try "never skips the last whole block twice (> 64)" 'while (x->n - i > 32 &&' 'while (x->n - i > 64 &&'
try "count not subtracted" '{ l -= k; i += 32; }' '{ i += 32; }'
cp /tmp/text.c.orig $f
