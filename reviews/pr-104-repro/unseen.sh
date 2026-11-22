#!/usr/bin/env bash
# unseen.sh <repo> <tool-rev> <base-rev> <rev>...: what the new check says about
# code it was not fitted to: findings on each rev that its base does not have
# (known list emptied, so every finding prints)
repo=$1 tool=$2 base=$3; shift 3
run() { t=$(mktemp -d); git -C $repo archive $1 src ref include | tar -x -C $t; mkdir -p $t/tests $t/tools
  git -C $repo show $tool:tools/dupes.py > $t/tools/dupes.py; git -C $repo show $tool:tests/owners.txt > $t/tests/owners.txt; : > $t/tests/dupes-known.txt
  (cd $t && python3 tools/dupes.py | sed 's/^FAIL //' | sed "s/.*   (\(.*\))$/\1/" | sort); rm -rf $t; }
run $base > /tmp/base.$$
for r in "$@"; do echo "== $r"; run $r | comm -13 /tmp/base.$$ - ; done
