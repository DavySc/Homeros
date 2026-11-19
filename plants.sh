#!/bin/sh
# Every plant in tests/plants.txt (or $PLANTS) must make ./check.sh fail
# (CLAUDE.md: a check never seen failing proves nothing).  Each runs in a
# scratch copy; a plant that does not apply counts as not caught.
cd "$(dirname "$0")"
here=$(pwd) main=$(cd "${HOMEROS:-../main}" && pwd) caught=0 total=0
# a scratch copy with no plant must pass, or every "caught" is void
# (the debugger prototype's lesson L33)
d=$(mktemp -d) && cp -r "$here"/sqlref "$here"/cmd "$here"/oracle "$here"/Makefile "$here"/check.sh "$d"/ &&
	HOMEROS="$main" "$d"/check.sh >/dev/null 2>&1 </dev/null || { echo "plants: the unplanted copy fails; nothing counted"; rm -rf "$d"; exit 1; }
rm -rf "$d"
grep -v '^#' "${PLANTS:-tests/plants.txt}" | grep -v '^$' > /tmp/plants.$$
while IFS='	' read -r file old new why; do
	total=$((total + 1))
	d=$(mktemp -d) || exit 1
	cp -r "$here"/sqlref "$here"/cmd "$here"/oracle "$here"/Makefile "$here"/check.sh "$d"/ || exit 1
	if python3 -I -c 'import sys; p,o,n=sys.argv[1:]; s=open(p).read(); assert s.count(o)==1; open(p,"w").write(s.replace(o,n))' "$d/$file" "$old" "$new" 2>/dev/null &&
	   ! HOMEROS="$main" "$d"/check.sh >/dev/null 2>&1 </dev/null; then
		caught=$((caught + 1))
	else
		echo "NOT CAUGHT: $file: $why"
	fi
	rm -rf "$d"
done < /tmp/plants.$$
rm -f /tmp/plants.$$
echo "plants: $caught of $total caught"
[ "$caught" = "$total" ]
