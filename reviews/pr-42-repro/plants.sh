#!/usr/bin/env bash
# Runs the plants of tests/plants/syntax.txt whose name matches a pattern, as
# tools/planted.sh does, but in one scratch copy that keeps build/syntax (so
# tree-sitter is not rebuilt per plant).  usage: plants.sh PATTERN
set -u
tmp=$(mktemp -d)
cp -r Makefile include ref src tests tools design "$tmp"/
mkdir -p "$tmp/build" && cp -r build/syntax "$tmp/build/"
grep -E "$1" tests/plants/syntax.txt | grep -v '^#' | while IFS='~' read -r name target prep file old new; do
	cp "$file" "$tmp/$file"
	if ! python3 - "$tmp/$file" "$old" "$new" <<'PY'
import sys
p, old, new = sys.argv[1], sys.argv[2].replace('¶', '\n'), sys.argv[3].replace('¶', '\n')
s = open(p).read()
if s.count(old) != 1: sys.exit('plant does not apply: %r' % old)
open(p, 'w').write(s.replace(old, new))
PY
	then echo "BROKEN PLANT  $name"; continue; fi
	if (cd "$tmp" && eval make $target) </dev/null >"$tmp.log" 2>&1
	then echo "NOT CAUGHT    $name"
	else echo "caught        $name: $(grep -a -m1 -E 'CHECK\(|[1-9][0-9]* of [0-9]+ [^.;]*differ|ERROR' "$tmp.log" | cut -c1-150)"; fi
	cp "$file" "$tmp/$file"
done
rm -rf "$tmp" "$tmp.log"
