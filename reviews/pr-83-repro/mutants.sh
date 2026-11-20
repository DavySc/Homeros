#!/usr/bin/env bash
# Review of PR 83: each line of mutants.txt planted in a copy of
# src/org/org.c, the driver rebuilt (the other objects built once) and the
# colour oracle run.  mutants.sh DRIVER-COMPILE-LINE-FILE WORKDIR
# (the compile line is `make -n org-conform | grep org_drv`)
set -u
line=$(cat "$1"); w=$2; mkdir -p "$w/o"
srcs=$(echo "$line" | grep -o ' \(src\|ref\)/[^ ]*\.c' | grep -v src/org/org.c)
flags=$(echo "$line" | sed 's/ -o build.*//')
objs=$(echo "$line" | grep -o ' build/[^ ]*\.o' | tr '\n' ' '); libs=$(echo "$line" | sed 's/.*\.o  *//')
for s in $srcs; do o=$w/o/$(echo $s | tr / _).o; [ -f $o ] || $flags -c $s -o $o || exit 1; done
while IFS='~' read -r name old new; do
	case $name in ''|'#'*) continue;; esac
	if ! python3 - src/org/org.c "$w/org.c" "$old" "$new" <<'PY'
import sys
old, new = sys.argv[3].replace('¶', '\n'), sys.argv[4].replace('¶', '\n')
s = open(sys.argv[1]).read()
if s.count(old) != 1: sys.exit(1)
open(sys.argv[2], 'w').write(s.replace(old, new))
PY
	then echo "BROKEN      $name"; continue; fi
	if ! $flags -Isrc/org -o $w/drv tests/org_drv.c $w/org.c $w/o/*.o $objs $libs 2>$w/cc.log; then echo "NO BUILD    $name"; continue; fi
	if python3 tools/org_conform.py -f $w/drv 2000 1 >$w/run.log 2>&1; then echo "NOT CAUGHT  $name"; else echo "caught      $name   ($(tail -1 $w/run.log))"; fi
done < "$(dirname "$0")/mutants.txt"
