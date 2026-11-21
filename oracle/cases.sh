#!/bin/sh
# Hand cases for refq on WWI, tab-separated, # comments:
#   tests/same.txt    expected (same, differ), old text, new text: the
#                     meaning check alone (refactor_same);
#   tests/rename.txt  column, new name, text, expected text (or REFUSED:
#                     and the reason's start).
bad=0 d=$(mktemp -d) s=/tmp/claude-0/WWI.snap
grep -v '^#' tests/same.txt | while IFS='	' read -r want a b; do
	printf %s "$b" > "$d/b"
	got=$(printf %s "$a" | build/refq $s same "$d/b")
	[ "$got" = "$want" ] || { echo "same.txt: expected $want, got $got: $a | $b"; echo x >> "$d/bad"; }
done
grep -v '^#' tests/rename.txt | while IFS='	' read -r col to a want; do
	got=$(printf %s "$a" | build/refq $s rename "$col" "$to")
	case "$got" in "$want") ;; REFUSED:*) [ "${got#"$want"}" != "$got" ] || { echo "rename.txt: expected $want, got $got: $a"; echo x >> "$d/bad"; };; *) echo "rename.txt: expected $want, got $got: $a"; echo x >> "$d/bad";; esac
done
n=$(cat "$d/bad" 2>/dev/null | wc -l); rm -rf "$d"
echo "tests/same.txt, tests/rename.txt: $n differ"; [ "$n" = 0 ]
