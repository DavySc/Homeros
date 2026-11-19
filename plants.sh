#!/bin/sh
# Every plant in tests/plants.txt (or $PLANTS) must make ./check.sh fail
# (CLAUDE.md: a check never seen failing proves nothing).  A plant line is
# file, text, replacement (may be empty), why: tab-separated.  Each runs in
# a scratch copy; a plant that does not apply counts as not caught.
cd "$(dirname "$0")"
here=$(pwd) main=$(cd "${HOMEROS:-../main}" && pwd) caught=0 total=0
list=$(cd "$(dirname "${PLANTS:-tests/plants.txt}")" && pwd)/$(basename "${PLANTS:-tests/plants.txt}")
copy() { d=$(mktemp -d) && cp -r "$here"/sqlref "$here"/cmd "$here"/oracle "$here"/tests "$here"/Makefile "$here"/check.sh "$d"/; }
# a scratch copy with no plant must pass, or every "caught" is void
# (the debugger prototype's lesson L33)
copy && HOMEROS="$main" "$d"/check.sh >/dev/null 2>&1 </dev/null || { echo "plants: the unplanted copy fails; nothing counted"; rm -rf "$d"; exit 1; }
rm -rf "$d"
n=$(grep -c '' "$list")
for line in $(seq 1 "$n"); do
	sed -n "${line}p" "$list" | grep -q '^[^#]' || continue
	total=$((total + 1))
	copy || exit 1
	why=$(python3 -I - "$list" "$line" "$d" <<'P'
import sys
f, old, new, why = open(sys.argv[1]).read().split('\n')[int(sys.argv[2]) - 1].split('\t')
p = sys.argv[3] + '/' + f
s = open(p).read()
print(why)
if s.count(old) != 1: sys.exit(1)
open(p, 'w').write(s.replace(old, new))
P
) && if HOMEROS="$main" "$d"/check.sh >/dev/null 2>&1 </dev/null; then false; else true; fi
	if [ $? = 0 ] && [ -n "$why" ]; then caught=$((caught + 1)); else echo "NOT CAUGHT: line $line: $why"; fi
	rm -rf "$d"
done
echo "plants: $caught of $total caught"
[ "$caught" = "$total" ]
