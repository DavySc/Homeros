#!/bin/sh
# tests/same.txt: the meaning check gives each expected verdict
bad=0 d=$(mktemp -d)
grep -v '^#' tests/same.txt | while IFS='	' read -r want a b; do
	printf %s "$b" > "$d/b"
	got=$(printf %s "$a" | build/refq /tmp/claude-0/WWI.snap same "$d/b")
	[ "$got" = "$want" ] || { echo "same.txt: expected $want, got $got: $a | $b"; echo x >> "$d/bad"; }
done
n=$(cat "$d/bad" 2>/dev/null | wc -l); rm -rf "$d"
echo "tests/same.txt: $n differ"; [ "$n" = 0 ]
