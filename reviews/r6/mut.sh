#!/bin/bash
# each mutation of muts.txt in a scratch copy; which of the package's tests catch it

while IFS='~' read -r name file old new; do
	[ -z "$name" ] && continue
	tmp=$(mktemp -d); cp -r Makefile include ref src tests tools "$tmp"/
	python3 - "$tmp/$file" "$old" "$new" <<'PY' || { echo "BROKEN  $name"; rm -rf $tmp; continue; }
import sys
p, old, new = sys.argv[1], sys.argv[2].replace('¶', '\n'), sys.argv[3].replace('¶', '\n')
s = open(p).read()
if s.count(old) != 1: sys.exit(1)
open(p, 'w').write(s.replace(old, new))
PY
	if (cd $tmp && make test TESTS="grid grid_diff tty_input tty_render") </dev/null >$tmp.log 2>&1
	then echo "SURVIVED  $name"
	else echo "caught    $name: $(grep -E 'CHECK\(|FAIL|error|ERROR' $tmp.log | head -1 | cut -c1-110)"; fi
	rm -rf $tmp $tmp.log
done < reviews/r6/muts.txt
