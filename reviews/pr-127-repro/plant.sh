#!/bin/bash
# plant.sh <plants file> <line number> <out>: the probe with that plant applied (planted.sh's replace)
set -e
R=/home/claude/Homeros
tmp=$(mktemp -d)
mkdir $tmp/src; cp -r $R/include $R/tests $tmp/; cp -r $R/src/org $R/src/text $R/src/uni $tmp/src/
IFS='~' read -r name target prep file old new < <(sed -n "$2p" "$1")
python3 - "$tmp/$file" "$old" "$new" <<'PY'
import sys
p, old, new = sys.argv[1], sys.argv[2].replace('¶', '\n'), sys.argv[3].replace('¶', '\n')
s = open(p).read()
if s.count(old) != 1: sys.exit('plant does not apply: %r' % old)
open(p, 'w').write(s.replace(old, new))
PY
"$(dirname "$0")/build.sh" $tmp "$3"
rm -rf $tmp
echo "$name"
