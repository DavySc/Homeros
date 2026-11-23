#!/usr/bin/env bash
# On real Windows (the probe run of PR 138's review): the PR's three Windows
# plants and the review's (plants.txt here), each applied to the tree, with
# src/infoview/check.sh run on it.  Prints caught or NOT CAUGHT.
cd "$(dirname "$0")/../.."
while IFS='~' read -r name target prep file old new; do
	python - "$file" "$old" "$new" <<'PY' || { echo "BROKEN PLANT  $name"; continue; }
import sys
p, old, new = sys.argv[1], sys.argv[2].replace('¶', '\n'), sys.argv[3].replace('¶', '\n')
s = open(p, encoding='utf-8').read()   # the runner's checkout may have CRLF
if s.count(old) != 1: sys.exit('plant does not apply: %r' % old)
open(p, 'w', encoding='utf-8', newline='\n').write(s.replace(old, new))
PY
	if bash src/infoview/check.sh > plant.log 2>&1; then echo "NOT CAUGHT    $name"
	else echo "caught        $name: $(grep -m1 'CHECK(' plant.log || tail -1 plant.log)"; fi
	taskkill //F //IM homeros-infoview.exe > /dev/null 2>&1
	git checkout -q -- src
done < <(grep -h "win WIN=infoview" tests/plants/infoview.txt; cat reviews/pr-138-repro/plants.txt)
