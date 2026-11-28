#!/usr/bin/env bash
# Under Wine: the PR's three Windows plants and the review's (plants.txt
# here), each applied to the tree, with src/infoview/check.sh run on it.
cd "$(dirname "$0")/../.."
while IFS='~' read -r name target prep file old new; do
	python3 - "$file" "$old" "$new" <<'PY' || { echo "BROKEN PLANT  $name"; continue; }
import sys
p, old, new = sys.argv[1], sys.argv[2].replace('¶', '\n'), sys.argv[3].replace('¶', '\n')
s = open(p).read()
if s.count(old) != 1: sys.exit('plant does not apply: %r' % old)
open(p, 'w').write(s.replace(old, new))
PY
	if bash src/infoview/check.sh > plant.log 2>&1; then echo "NOT CAUGHT    $name"
	else echo "caught        $name: $(grep -m1 'CHECK(' plant.log || tail -1 plant.log)"; fi
	WINEPREFIX=$PWD/build/win/prefix-infoview wineserver -k 2>/dev/null
	git checkout -q -- src
done < <(if [ "${1:-}" ]; then grep -h "$1" reviews/pr-138-repro/plants.txt; else grep -h "win WIN=infoview" tests/plants/infoview.txt; cat reviews/pr-138-repro/plants.txt; fi)
rm -f plant.log
