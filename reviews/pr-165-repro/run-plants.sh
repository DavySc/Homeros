#!/bin/bash
# usage: run-plants.sh FILE LINK.SH  (in the repo root; each line: name~file~old~new, ¶ a newline)
# builds tests/test_lsp_locs with the plant and runs it: caught when it fails
while IFS='~' read -r name file old new; do
	python3 - "$file" "$old" "$new" <<'PY' || { echo "NOT APPLIED  $name"; continue; }
import sys
f, old, new = sys.argv[1], sys.argv[2].replace('¶', '\n'), sys.argv[3].replace('¶', '\n')
s = open(f).read()
if s.count(old) != 1: sys.exit(1)
open(f, 'w').write(s.replace(old, new))
PY
	if ! "$2" build/rv/t_plant tests/test_lsp_locs.c >build/rv/plant.log 2>&1; then r="caught (does not compile)"
	elif timeout 600 ./build/rv/t_plant >>build/rv/plant.log 2>&1; then r="MISSED"; else r="caught: $(tail -1 build/rv/plant.log | cut -c1-90)"; fi
	git checkout -q "$file"
	echo "$r  $name"
done < "$1"
"$2" build/rv/t_plant tests/test_lsp_locs.c
