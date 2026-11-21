#!/bin/sh
# Hand cases for refq on WWI, tab-separated, # comments:
#   tests/same.txt       expected (same, differ), old text, new text: the
#                        meaning check alone (refactor_same);
#   tests/refactors.txt  op, argument, argument, text, expected (its header).
bad=0 d=$(mktemp -d) s=${SNAPS:-/tmp/claude-0}/WWI.snap
grep -v '^#' tests/same.txt | while IFS='	' read -r want a b; do
	printf %s "$b" > "$d/b"
	got=$(printf %s "$a" | build/refq $s same "$d/b")
	[ "$got" = "$want" ] || { echo "same.txt: expected $want, got $got: $a | $b"; echo x >> "$d/bad"; }
done
grep -v '^#' tests/refactors.txt | while IFS='	' read -r op x y a want; do
	case $op in rename) set -- "$x" "$y";; *) at=$(printf %s "${a%%"$x"*}" | wc -c); set -- $at; [ "$y" = - ] || set -- $at "$y";; esac
	got=$(printf %s "$a" | build/refq $s $op "$@")
	case "$got" in "$want") ;; REFUSED:*) [ "${got#"$want"}" != "$got" ] || { echo "refactors.txt: expected $want, got $got: $op $a"; echo x >> "$d/bad"; };; *) echo "refactors.txt: expected $want, got $got: $op $a"; echo x >> "$d/bad";; esac
done
n=$(cat "$d/bad" 2>/dev/null | wc -l); rm -rf "$d"
echo "tests/same.txt, tests/refactors.txt: $n differ"; [ "$n" = 0 ]
