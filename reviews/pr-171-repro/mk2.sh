#!/bin/bash
# usage: mk.sh <tree> <test.c> <out>  -- builds a test against the tree's sources with sanitizers, objects cached per tree
set -e
tree=$1; src=$2; out=$3
cd $tree
line=$(make -n test TESTS=ZZZ 2>/dev/null | grep -m1 'tests/test_\$t.c')
flags=$(echo "$line" | sed 's/ -o build.*//; s/^ *gcc//')
srcs=$(echo "$line" | sed 's/.*tests\/test_\$t.c //; s/ || exit 1.*//')
od=build/rvobj
if [ -n "$O2" ]; then flags=$(echo "$flags" | sed 's/-O1/-O2/; s/-fsanitize=[^ ]*//g; s/-fno-sanitize-recover=all//'); od=build/rvobj2; fi
mkdir -p $od
objs=""
for f in $srcs; do
  case $f in
   *.c) o=$od/$(echo $f | tr / _).o
        if [ ! -e $o ] || [ $f -nt $o ]; then echo "gcc $flags -c -o $o $f"; fi
        objs="$objs $o";;
   *) objs="$objs $f";;
  esac
done > $od/cmds.txt
grep '^gcc' $od/cmds.txt | xargs -P4 -I{} sh -c '{}' || true
objs=""
for f in $srcs; do case $f in *.c) objs="$objs $od/$(echo $f | tr / _).o";; *) objs="$objs $f";; esac; done
gcc $flags $EXTRA -Wno-error -Wno-unused-result -Itests -Isrc/sql -o $out $src $objs
