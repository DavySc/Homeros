#!/usr/bin/env bash
# variants.sh <repo>: which of variants.c's functions does make dupes flag?
t=$(mktemp -d); cp -r "$1"/{src,ref,include,tests,tools} $t/; cp "$(dirname "$0")/variants.c" $t/src/tools/zz.c
gcc -std=c17 -Wall -Wextra -Werror -c $t/src/tools/zz.c -o /dev/null || exit 1
(cd $t && python3 tools/dupes.py) | while read -r l; do
  n=$(echo "$l" | grep -o 'zz.c:[0-9]*' | cut -d: -f2)
  echo "$l" | cut -c1-60; awk -v n=$n 'NR<=n && /^static/ {f=$0} END{print "   in: " f}' $t/src/tools/zz.c; done
rm -rf $t
