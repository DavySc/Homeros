#!/bin/bash
# PR 98 review: each line of MUTANTS (name~old~new, ¶ a newline) put into
# src/org/org.c, then run against the unit test and against Org on the
# check's own TODO texts (org_conform.py -t, 500 texts, seed 1) and on DIRs.
# mutants.sh MUTANTS DIR...   (run in the repo, after one `make org-conform`)
here=$(cd "$(dirname "$0")" && pwd); mf=$1; shift
cc=$(make -n org-conform 2>/dev/null | grep -m1 'org_drv tests' | sed 's#src/org/org.c#build/v/org.c#')
while IFS='~' read -r name old new; do
	[ -z "$name" ] && continue
	mkdir -p build/v
	python3 - src/org/org.c "$old" "$new" build/v/org.c <<'PY' || { echo "DOES NOT APPLY  $name"; continue; }
import sys
p, old, new, out = sys.argv[1], sys.argv[2].replace('¶', '\n'), sys.argv[3].replace('¶', '\n'), sys.argv[4]
s = open(p).read()
if s.count(old) != 1: sys.exit(1)
open(out, 'w').write(s.replace(old, new))
PY
	eval "${cc/-o build\/san\/org_drv/-o build/v/drv}" || { echo "DOES NOT BUILD  $name"; continue; }
	eval "${cc/-o build\/san\/org_drv tests\/org_drv.c/-o build/v/test tests/test_org.c}" || { echo "DOES NOT BUILD  $name"; continue; }
	res="unit test: $(build/v/test >/dev/null 2>&1 && echo SURVIVES || echo caught)"
	res="$res; check's texts: $(python3 tools/org_conform.py -t build/v/drv 500 1 | tail -1 | grep -q '^0 of' && echo SURVIVES || echo caught)"
	for d in "$@"; do res="$res; $(basename $d): $(python3 tools/org_conform.py -t build/v/drv $d/*.org | tail -1 | grep -q '^0 of' && echo SURVIVES || echo caught)"; done
	rm -f build/org-fail-*
	echo "$name  =>  $res"
done < "$mf"
