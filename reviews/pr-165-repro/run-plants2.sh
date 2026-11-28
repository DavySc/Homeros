#!/bin/bash
# usage: run-plants2.sh FILE LINK.SH  (in the repo root; each line: name~TEST~file~old~new, ¶ a newline)
# builds tests/test_TEST.c with the plant and runs it: caught when it fails
while IFS='~' read -r name t file old new; do
	python3 - "$file" "$old" "$new" <<'PY' || { echo "NOT APPLIED  $name"; continue; }
import sys
f, old, new = sys.argv[1], sys.argv[2].replace('¶', '\n'), sys.argv[3].replace('¶', '\n')
s = open(f).read()
if s.count(old) != 1: sys.exit(1)
open(f, 'w').write(s.replace(old, new))
PY
	if ! "$2" build/rv/t_plant tests/test_$t.c >build/rv/plant.log 2>&1; then r="caught (does not compile)"
	elif timeout 900 ./build/rv/t_plant >>build/rv/plant.log 2>&1; then r="MISSED"; else r="caught: $(grep -a 'CHECK\|differ\|runtime error\|FAIL' build/rv/plant.log | tail -1 | cut -c1-90)"; fi
	git checkout -q "$file"
	echo "$r  [$t] $name"
done < "$1"
