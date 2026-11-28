#!/bin/bash
# usage: run-plants.sh TREE ARCHIVE_DIR PLANTS.txt   (lines as tests/plants/*.txt: name~test TESTS=x~prep~file~old~new)
# Each plant: the one file patched in a scratch copy, compiled with gcc (sanitizers) and clang (-fsyntax-only),
# both -Werror, linked before the tree's archive, the test run from TREE.  Prints CAUGHT / NOT CAUGHT / BROKEN.
T=$1; O=$2; . $O/vars; i=0
while IFS='~' read -r name target prep file old new; do
  [ -z "$name" ] && continue; i=$((i+1)); t=${target#test TESTS=}; d=$(mktemp -d)
  python3 - "$T/$file" "$d/p.c" "$old" "$new" <<'PY' || { echo "BROKEN (does not apply once)  $name"; continue; }
import sys
old, new = sys.argv[3].replace('¶', '\n'), sys.argv[4].replace('¶', '\n')
s = open(sys.argv[1]).read()
if s.count(old) != 1: sys.exit(1)
open(sys.argv[2], 'w').write(s.replace(old, new))
PY
  inc="-I$T/$(dirname $file)"
  (cd $T && gcc $FLAGS $inc -c -o $d/p.o $d/p.c 2>$d/gcc.log) || { echo "BROKEN (gcc: $(grep -m1 error $d/gcc.log))  $name"; continue; }
  (cd $T && clang $OPT $inc -fsyntax-only $d/p.c 2>$d/clang.log) || { echo "BROKEN (clang: $(grep -m1 error $d/clang.log))  $name"; continue; }
  (cd $T && gcc $FLAGS -o $d/t tests/test_$t.c $d/p.o $O/san.a $LINK 2>$d/link.log) || { echo "BROKEN (link)  $name"; continue; }
  if (cd $T && timeout 900 $d/t >$d/out.log 2>&1); then echo "NOT CAUGHT  $name"; else echo "CAUGHT  $name  [$(grep -v '^$' $d/out.log | tail -1 | cut -c1-90)]"; fi
  rm -rf $d
done < $3
