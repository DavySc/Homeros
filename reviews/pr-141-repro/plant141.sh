#!/usr/bin/env bash
# run.sh PLANTFILE [start [count]]: each plant applied to a scratch copy, test_snip built there with the Makefile's command and run
src=/home/claude/pr141
i=0
while IFS='~' read -r name target prep file old new; do
	[ -z "$name" ] && continue
	i=$((i+1)); [ $i -lt ${2:-1} ] && continue; [ -n "$3" ] && [ $i -ge $(( $2 + $3 )) ] && break
	tmp=$(mktemp -d /tmp/rv/plants/w.XXXXXX)
	(cd $src && cp -r Makefile include ref src tests "$tmp"/) ; rm -f "$tmp"/tests/test_z*.c
	mkdir -p "$tmp/build/san"; for d in syntax ghostty wl image; do ln -s $src/build/$d "$tmp/build/$d"; done
	if ! python3 - "$tmp/$file" "$old" "$new" <<'PY'
import sys
p, old, new = sys.argv[1], sys.argv[2].replace('¶', '\n'), sys.argv[3].replace('¶', '\n')
s = open(p).read()
if s.count(old) != 1: sys.exit('plant does not apply: %r' % old)
open(p, 'w').write(s.replace(old, new))
PY
	then echo "BROKEN PLANT  $name"; rm -rf "$tmp"; continue; fi
	if ! (cd "$tmp" && bash <(sed 's/test_zprobe141/test_snip/g' /tmp/rv/build.sh)) > "$tmp.log" 2>&1; then echo "BUILD FAILED  $name"; tail -3 "$tmp.log"; rm -rf "$tmp"; continue; fi
	if (cd "$tmp" && XDG_STATE_HOME=$tmp/state timeout 900 ./build/san/test_snip) >> "$tmp.log" 2>&1; then echo "NOT CAUGHT    $name"; else echo "caught        $name   ($(grep -m1 -o "CHECK.*\|ERROR: AddressSanitizer: [a-z-]*\|runtime error.*" "$tmp.log" | cut -c1-70))"; fi
	rm -rf "$tmp"
done < "$1"
