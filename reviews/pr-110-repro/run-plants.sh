#!/usr/bin/env bash
# tools/planted.sh's loop for one plants file, reusing the built grammars
# (build/syntax is copied with its times, so each plant compiles only our
# code): run-plants.sh FILE [JOBS]
cd "$(dirname "$0")/../.."
one() {
	IFS='~' read -r name target prep file old new <<<"$1"
	tmp=$(mktemp -d)
	cp -r Makefile assets include ref src tests tools design "$tmp"/
	mkdir "$tmp/build" && cp -a build/syntax "$tmp/build/"
	touch -r build/syntax/queries.c "$tmp/src/syntax/build.mk"
	if ! python3 -c "$APPLY" "$tmp/$file" "$old" "$new"
	then echo "BROKEN PLANT  $name"; rm -rf "$tmp"; return; fi
	if (cd "$tmp" && eval "$prep" && eval make -s $target) </dev/null >"$tmp.log" 2>&1
	then echo "NOT CAUGHT    $name   (make $target passed)"
	else
		cc='^$'
		case $target in compilers*|wincc*) ;; *) cc='\.[ch]:[0-9]+(:[0-9]+)?: (fatal )?(error|warning):|^cc1|undefined reference|ld returned|^[0-9]+ errors? generated\.$';; esac
		why=$(grep -P 'CHECK\(|(?<!of )\b[1-9][0-9]* differ|\b[1-9][0-9]* of [0-9]+ [^.;]*?differ|WRONG|error|ERROR|Proved goals|sorry|deadly signal|FAIL' "$tmp.log" | grep -v -E '^(gcc|clang|cc) |^make|No rule|got error' | grep -v -E "$cc" | head -1)
		if [ -n "$why" ]; then echo "caught        $name   (make $target): $why"
		else echo "FAILED OTHERWISE  $name   (make $target): $(tail -1 "$tmp.log")"; fi
	fi
	rm -rf "$tmp" "$tmp.log"
}
export -f one
export APPLY="import sys
p, old, new = sys.argv[1], sys.argv[2].replace('¶', '\\n'), sys.argv[3].replace('¶', '\\n')
s = open(p).read()
if s.count(old) != 1: sys.exit('plant does not apply: %r' % old)
open(p, 'w').write(s.replace(old, new))"
grep -v -e "^#" -e "^$" "$1" | xargs -d '\n' -P "${2:-3}" -I{} bash -c 'one "$1"' _ {}
