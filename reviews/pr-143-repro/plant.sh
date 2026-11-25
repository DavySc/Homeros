#!/bin/bash
# plant.sh <plants file> <line number> <out> [probe]: test_org (or the probe) with that plant applied
set -e
R=${R:-/home/claude/Homeros}
d=$(dirname "$(readlink -f "$0")")
tmp=$(mktemp -d)
mkdir $tmp/src; cp -r $R/include $R/tests $tmp/; cp -r $R/src/org $R/src/text $R/src/uni $R/src/date $tmp/src/
IFS='~' read -r name target prep file old new < <(sed -n "$2p" "$1")
python3 - "$tmp/$file" "$old" "$new" <<'PY'
import sys
p, old, new = sys.argv[1], sys.argv[2].replace('¶', '\n'), sys.argv[3].replace('¶', '\n')
s = open(p).read()
if s.count(old) != 1: sys.exit('plant does not apply: %r' % old)
open(p, 'w').write(s.replace(old, new))
PY
"$d/build.sh" $tmp "$3" $4
rm -rf $tmp
echo "$name"
