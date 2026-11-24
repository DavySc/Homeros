#!/usr/bin/env bash
# On Windows itself (a probe run of PR 138's review): line $1 of plants.txt
# applied (0: none), then src/infoview/check.sh with the test exe CI's winexe
# job built.  Exit 0: caught (or, for 0, the test passes $2 times); 1: NOT
# CAUGHT.  A plant that hangs the test ends by the job's time limit.
cd "$(dirname "$0")/../.."
if [ "$1" = 0 ]; then
	for i in $(seq "${2:-3}"); do bash src/infoview/check.sh < /dev/null || { echo "FAIL without a plant, run $i"; exit 1; }; done
	echo "passes ${2:-3} of ${2:-3} without a plant"; exit 0
fi
IFS='~' read -r name target prep file old new <<< "$(sed -n "$1p" reviews/pr-138-repro/plants.txt)"
echo "plant $1 on $(uname -s): $name"
python - "$file" "$old" "$new" <<'PY' || { echo "BROKEN PLANT  $name"; exit 2; }
import sys
p, old, new = sys.argv[1], sys.argv[2].replace('¶', '\n'), sys.argv[3].replace('¶', '\n')
s = open(p, encoding='utf-8').read()   # the runner's checkout may have CRLF
if s.count(old) != 1: sys.exit('plant does not apply: %r' % old)
open(p, 'w', encoding='utf-8', newline='\n').write(s.replace(old, new))
PY
if bash src/infoview/check.sh < /dev/null > plant.log 2>&1; then echo "NOT CAUGHT    $name"; exit 1
else echo "caught        $name: $(grep -m1 'CHECK(' plant.log || tail -1 plant.log)"; fi
