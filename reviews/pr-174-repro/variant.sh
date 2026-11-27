#!/usr/bin/env bash
# variant.sh NAME [PLANTLINE | FILE OLD NEW]: a copy of the tree with one
# plant (a line number of tests/plants/syntax.txt, or file/old/new), its
# test and probe built as make test builds them.  Prints the directory.
set -e
R=$(dirname "$(readlink -f "$0")"); H=${H:-$(git rev-parse --show-toplevel)}
name=$1; d=$R/v/$name; rm -rf "$d"; mkdir -p "$d/build/san"
cp -r $H/Makefile $H/include $H/ref $H/src $H/tests $H/tools "$d"/
for b in $H/build/*; do [ "$(basename $b)" = san ] || ln -s "$b" "$d/build/"; done
if [ $# -eq 2 ]; then
  IFS='~' read -r _ _ _ file old new < <(sed -n "${2}p" $H/tests/plants/syntax.txt)
elif [ $# -eq 4 ]; then file=$2 old=$3 new=$4; fi
if [ -n "$file" ]; then python3 - "$d/$file" "$old" "$new" <<'PY'
import sys
p, old, new = sys.argv[1], sys.argv[2].replace('¶', '\n'), sys.argv[3].replace('¶', '\n')
s = open(p).read()
if s.count(old) != 1: sys.exit('plant does not apply: %r' % old)
open(p, 'w').write(s.replace(old, new))
PY
fi
cd "$d"; python3 $R/mkprobe.py tests/test_syntax_lean.c tests/test_probe.c
cc=$(cat $R/cc.txt)
eval "${cc//@T@/syntax_lean}" & eval "${cc//@T@/probe}" & wait
echo "$d"
