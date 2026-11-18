#!/usr/bin/env bash
# Review of PR 56: mutations of the new code that the PR's plants do not
# cover.  For each line of muts.txt (name~file~old~new) applies it to a copy
# of TREE, builds tests/test_core_markdown.c and mdcmp.c with ASan+UBSan and
# says whether the PR's 300 recorded screens and the review's live-recorded
# cases (CASES...) notice.  usage: mut.sh TREE CASES.txt...   (TREE built)
here=$(cd "$(dirname "$0")" && pwd)
tree=$1; shift
while IFS='~' read -r name file old new; do
	[ -z "$name" ] && continue
	tmp=$(mktemp -d)
	cp -r "$tree"/. "$tmp"/
	python3 - "$tmp/$file" "$old" "$new" <<'PY' || { echo "BROKEN $name"; rm -rf "$tmp"; continue; }
import sys
p, old, new = sys.argv[1], sys.argv[2].replace('¶', '\n'), sys.argv[3].replace('¶', '\n')
s = open(p).read()
if s.count(old) != 1: sys.exit('does not apply: %r' % old)
open(p, 'w').write(s.replace(old, new))
PY
	cp "$tmp/tests/test_core_markdown.c" "$here/.tcm.c"
	san="-O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -Werror"
	if ! { "$here/build.sh" "$tmp" .tcm.c "$tmp/tcm" $san && "$here/build.sh" "$tmp" mdcmp.c "$tmp/mdcmp" $san; } >"$tmp.log" 2>&1; then
		echo "NO BUILD  $name: $(grep -m1 error "$tmp.log")"; rm -rf "$tmp" "$here/.tcm.c"; continue; fi
	pr=$(cd "$tmp" && ./tcm 2>&1 | tail -1)
	case "$pr" in *"screens: 0 differ"*) a="PR's test: NOT CAUGHT";; *) a="PR's test: caught ($pr)";; esac
	b=""
	for c in "$@"; do b="$b $("$tmp/mdcmp" "$c" 2>&1 | tail -1 | sed 's/mdcmp: //')"; done
	echo "$name | $a | review cases:$b"
	rm -rf "$tmp" "$tmp.log" "$here/.tcm.c"
done < "$here/muts.txt"
