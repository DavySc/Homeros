#!/bin/bash
# plants.sh TREE FILE JOBS: each plant of FILE (tests/plants format) in a copy of TREE with its build/ objects
export PATH=/home/claude/tools-env/bin:$PATH
one() {
	IFS='~' read -r name target prep file old new <<< "$1"
	tmp=$(mktemp -d)
	cp -r "$TREE"/{Makefile,assets,include,ref,src,tests,tools,design} "$tmp"/
	mkdir "$tmp/build" && cp -r "$TREE"/build/{syntax,wl} "$tmp/build/"
	python3 - "$tmp/$file" "$old" "$new" <<'PY' || { echo "BROKEN PLANT  $name"; rm -rf "$tmp"; return; }
import sys
p, old, new = sys.argv[1], sys.argv[2].replace('¶', '\n'), sys.argv[3].replace('¶', '\n')
s = open(p).read()
if s.count(old) != 1: sys.exit('plant does not apply: %r' % old)
open(p, 'w').write(s.replace(old, new))
PY
	if (cd "$tmp" && eval make -s $target) </dev/null >"$tmp.log" 2>&1; then echo "NOT CAUGHT    $name"
	else why=$(grep -P 'CHECK\(|(?<!of )\b[1-9][0-9]* differ|\b[1-9][0-9]* of [0-9]+ [^.;]*?differ|deadly signal|FAIL|AddressSanitizer|runtime error' "$tmp.log" | grep -v -E '^(gcc|clang|cc) ' | head -1)
		cc=$(grep -E '\.[ch]:[0-9]+(:[0-9]+)?: (fatal )?(error|warning):' "$tmp.log" | head -1)
		if [ -n "$cc" ]; then echo "BUILD BROKEN  $name: $cc"; elif [ -n "$why" ]; then echo "caught        $name: ${why:0:150}"; else echo "FAILED OTHERWISE $name: $(tail -1 "$tmp.log")"; fi
	fi
	rm -rf "$tmp" "$tmp.log"
}
export -f one; export TREE=$1
grep -v -e '^#' -e '^$' "$2" | xargs -d '\n' -P "$3" -I{} bash -c 'one "$1"' _ {}
